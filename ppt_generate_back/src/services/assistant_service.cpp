#include "services/assistant_service.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

#include "logger.h"
#include "models/material.h"
#include "models/ppt_request.h"
#include "models/ppt_template.h"
#include "services/ai_search_service.h"
#include "services/material_service.h"
#include "services/ppt_service.h"
#include "services/template_manager_service.h"
#include "services/template_service.h"
#include "utils/settings_reader.h"

namespace {

constexpr const char* kQwenEndpoint =
    "https://dashscope.aliyuncs.com/api/v1/services/aigc/text-generation/generation";

constexpr const char* kDefaultModel = "qwen-plus";

constexpr int kContextMessages = 10;
constexpr int kMaxToolCallRounds = 3;

// 客户端工具集合（需透传给前端执行）
const std::set<std::string> kClientTools = {
    "navigate_to_page",
    "trigger_generate_ppt",
    "open_ppt_editor",
    "download_ppt",
    "batch_download_ppt",
    "toggle_maintenance_mode",
    "batch_delete_ppt",   // 批量删除前端处理：弹出含清单的确认框，用户确认后前端调后端 API
    "show_material_upload",
    "preview_material",
    "fill_login_form",
    "show_announcement",
};

// 需要二次确认的危险工具（普通确认弹窗）
const std::set<std::string> kDangerTools = {
    "delete_ppt",
    "batch_delete_ppt",
    "delete_material",
    "admin_toggle_template",
    "admin_delete_material",
    "create_announcement",
};

// 需要输入 "CONFIRM" 的最高级危险工具（特殊确认弹窗）
const std::set<std::string> kHighDangerTools = {
    "toggle_maintenance_mode",
};

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
            const auto& content = choice["message"]["content"];
            if (content.is_string()) return content.get<std::string>();
          }
        }
      }
    }
  } catch (...) {}
  return {};
}

// 提取 Qwen 返回的 tool_calls 数组
nlohmann::json ExtractToolCalls(const nlohmann::json& resp) {
  try {
    if (resp.contains("output") && resp["output"].contains("choices") &&
        resp["output"]["choices"].is_array()) {
      for (const auto& choice : resp["output"]["choices"]) {
        if (choice.contains("message") && choice["message"].contains("tool_calls")) {
          const auto& tc = choice["message"]["tool_calls"];
          if (tc.is_array() && !tc.empty()) return tc;
        }
      }
    }
  } catch (...) {}
  return nlohmann::json::array();
}

std::string ExtractJsonFromText(const std::string& text) {
  auto start = text.find('{');
  auto end   = text.rfind('}');
  if (start != std::string::npos && end != std::string::npos && end > start) {
    return text.substr(start, end - start + 1);
  }
  return text;
}

// 从 AI 返回文本中提取 JSON 数组（去除 markdown 代码块包裹）
static std::string ExtractJsonArrayFromText(const std::string& text) {
  std::string cleaned = text;
  auto fence = cleaned.find("```");
  if (fence != std::string::npos) {
    auto nl = cleaned.find('\n', fence);
    if (nl != std::string::npos) cleaned = cleaned.substr(nl + 1);
    auto end_fence = cleaned.rfind("```");
    if (end_fence != std::string::npos) cleaned = cleaned.substr(0, end_fence);
  }
  auto s = cleaned.find('[');
  auto e = cleaned.rfind(']');
  if (s != std::string::npos && e != std::string::npos && e > s)
    return cleaned.substr(s, e - s + 1);
  return cleaned;
}

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

std::string NowISO() {
  auto now  = std::chrono::system_clock::now();
  auto time = std::chrono::system_clock::to_time_t(now);
  std::ostringstream oss;
  oss << std::put_time(std::gmtime(&time), "%Y-%m-%dT%H:%M:%SZ");
  return oss.str();
}

std::string MakeTitle(const std::string& message) {
  if (message.size() <= 30) return message;
  std::string title = message.substr(0, 30);
  title += "...";
  return title;
}

// 截断 tool_result 防止 context 溢出
std::string TruncateToolResult(const std::string& s, size_t max_len = 2000) {
  if (s.size() <= max_len) return s;
  return s.substr(0, max_len) + "...(已截断)";
}

}  // namespace

// ── 静态哑变量 ────────────────────────────────────────────────────────────────
std::string AssistantService::dummy_error_;

// ── 构造函数 ──────────────────────────────────────────────────────────────────
AssistantService::AssistantService(std::string api_key,
                                   std::uint32_t timeout_seconds,
                                   std::shared_ptr<MongoClient> mongo,
                                   std::shared_ptr<MySQLConnectionPool> pool,
                                   std::shared_ptr<PptService> ppt_service,
                                   std::shared_ptr<MaterialService> material_service,
                                   std::shared_ptr<TemplateManagerService> tmpl_mgr_service,
                                   std::shared_ptr<TemplateService> template_service,
                                   std::shared_ptr<AiSearchService> ai_search_service)
    : api_key_(std::move(api_key)),
      timeout_seconds_(timeout_seconds),
      mongo_(std::move(mongo)),
      pool_(std::move(pool)),
      ppt_service_(std::move(ppt_service)),
      material_service_(std::move(material_service)),
      tmpl_mgr_service_(std::move(tmpl_mgr_service)),
      template_service_(std::move(template_service)),
      ai_search_service_(std::move(ai_search_service)) {}

// ── System Prompt ─────────────────────────────────────────────────────────────
std::string AssistantService::BuildSystemPrompt(bool is_admin) const {
  std::string prompt = R"(你是PPT生成系统的AI助手，名字叫"四夕丽人土土"，简称"四夕"。请以"四夕"自称，不要使用"土土"。
你的职责是帮助用户操作PPT生成系统，以友好、简洁的中文回复。

你拥有一系列工具可以调用，工具分为两类：
1. 服务端工具：由后端直接执行（如查询PPT历史、删除PPT等），你调用后会收到执行结果。
2. 客户端工具：由前端执行（如页面跳转、打开编辑器等），你调用后系统会在前端执行相应操作。

使用原则：
- 优先通过工具完成用户需求，而不是仅给出文字指导。
- 执行危险操作（删除等）前，在 confirm_text 参数中明确告知后果。
- 若工具执行结果中有数据，在 reply 中做简洁总结，不要原样重复所有数据。
- 若用户请求超出工具能力范围，友好说明限制并给出替代建议。
- 回复语气亲切、简洁，可用「四夕」第一人称。

【批量操作强制流程 - 与幻觉规则同等优先级】
- 用户说「批量删除」「删除所有关于XX的PPT」→ 必须先调用 search_ppt_history 获取真实列表，将结果传入 batch_delete_ppt 的 ppt_list 参数
- 用户说「批量下载」「下载这两个」「下载第一个和第二个」→ 必须先调用 search_ppt_history 获取真实列表，再从结果中精确匹配用户指定的 PPT（按标题匹配），将匹配到的完整记录（id、title、topic、template_name、pages、created_at、has_file 所有可用字段）原样传入 batch_download_ppt 的 ppt_list，不得只传 id 和 title
- 严禁自行构造 ppt_list 内容，所有 id 必须来自工具返回的真实数据
- 严禁用「第一个」「第二个」等序号作为依据随意选取 PPT；用户说「第一个」时，必须结合当前会话中 search_ppt_history 返回的列表顺序来确定，不得从其他上下文自行猜测
- 用户明确说出 PPT 标题片段时（如「帮我下载 IPPTGen 和 PASS 这两个」），必须在 search_ppt_history 结果中按标题匹配，取匹配到的真实 id，不得猜测或替换为其他 PPT
- 单次批量删除不超过 20 条，超过时分批处理并提醒用户
- 批量操作前必须在文字回复中向用户说明将要操作的具体 PPT 标题（不是序号）

【绝对禁止幻觉 - 最高优先级，任何情况下不得违反】
- 严禁编造任何数据：文件名、素材ID、用户ID、时间、数量、审核状态等所有具体数值，必须来自工具调用的真实返回结果
- 如果需要展示列表、查询某条记录、获取任何系统数据，必须先调用对应工具，绝对不允许凭记忆或猜测给出任何具体数据
- 如果工具尚未调用或结果为空，回复中只能说「请稍等，四夕正在查询」或「需要先查询，请告诉四夕查哪个范围的数据」，不得捏造任何内容
- 违反此规则会导致用户对错误信息执行操作，造成严重后果，绝对不允许发生)";

  if (is_admin) {
    prompt += R"(

【管理员专属规则 - 必须严格遵守】
你当前以管理员身份运行。管理员同时拥有普通用户工具和管理员专属工具。

⚠️ 关键规则：当用户说「素材」「文件」「PPT」等词语时，含义存在歧义——
可能指"管理员自己的数据"，也可能指"全系统所有用户的数据"。

【强制要求】：当请求范围存在歧义时，必须先询问再调工具；当范围明确时，必须直接调工具获取真实数据，绝不允许凭空回复：
- 用户说「我的素材」→ 直接调 list_materials，不询问
- 用户说「全部素材」/「所有用户的素材」/「全系统素材」→ 直接调 admin_list_materials，不询问
- 用户说「待审核素材」（未说明范围）→ 先询问：「请问是您自己的待审核素材，还是全系统所有用户的待审核素材？」
- 用户说「查看第一个素材」/「预览这个素材」等明确指向当前已有列表的操作 → 若当前会话已有工具返回的列表数据，直接操作；若没有，先调工具获取列表
- 任何情况下，你回复的文件名、ID、时间、数量等具体数据，必须来自工具返回，不得编造

工具对应关系（用户确认后才调用）：
- 管理员自己的素材 → list_materials
- 全系统素材 / 待审核（全局）→ admin_list_materials，且「待审核」必须传 review_status="unreviewed"
- 管理员自己的PPT → search_ppt_history
- 全系统PPT统计、生成成功率 → get_system_stats
- 热门关键词 → get_insights(sections=["keywords"])
- 常用模板排行 → get_insights(sections=["templates"])
- 页数分布 → get_insights(sections=["pages"])
- 用户留存漏斗 → get_insights(sections=["funnel"])
- 完整偏好洞察报告 / 偏好分析摘要 → get_insights(sections=["keywords","templates","pages","funnel"])
- 开启维护模式 / 进入维护 → 立即调用 toggle_maintenance_mode(action="enable", confirm_code="", confirm_text="...")
- 关闭维护模式 / 结束维护 / 恢复系统 → 立即调用 toggle_maintenance_mode(action="disable", confirm_code="", confirm_text="...")

【维护模式工具调用规则 - 最高优先级，违反即为严重错误】
- 用户意图为「开启/关闭维护模式」时，你的唯一正确行为是触发 toggle_maintenance_mode function call
- 严禁用文字声称「已为您发起请求」「已发起操作」「操作已完成」等——这是幻觉，必须通过 function call 触发
- confirm_code 参数必须传空字符串 ""，不得传 "CONFIRM" 或任何其他值
- 工具被调用后前端会自动处理后续的用户确认流程，你无需在回复中描述这个过程
- 如果你只是用文字描述而没有调用工具，等同于什么都没做，这是不可接受的

【严禁幻觉ID - 最高优先级规则，绝对不得违反】
- 调用 admin_review_material / admin_delete_material / delete_material 时，material_id 字段的值必须来自同一会话中工具调用结果里真实存在的 id 字段值
- 严禁自行编造、猜测、从训练知识中生成任何 material_id 或 ppt_id
- 如果用户说「审核/删除某文件名」但当前会话中没有该记录的真实 id，必须先调 admin_list_materials 搜索得到真实 id，再执行操作
- 搜索后仍找不到则告知用户「未找到该素材，请确认文件名是否正确」，不得对不存在的记录执行任何写操作

【模板操作强制流程 - 与上面规则同等优先级】
- 用户说「上架/下架某模板」时，必须先调 list_templates（传 limit=20 获取完整列表），从返回的 data 中找到对应名称的真实 id，再调 admin_toggle_template
- 严禁根据模板名称自行猜测 template_id，即使名称看起来很明确
- list_templates 返回的 data 中每条记录都有 is_active 字段：is_active=true 表示已上架，is_active=false 表示未上架但模板存在
- 用户要「上架」某模板时，在 data 中找名称匹配的条目（无论 is_active 是 true 还是 false），取其 id 调用 admin_toggle_template(action="activate")
- 用户要「下架」某模板时，在 data 中找名称匹配的条目（无论 is_active 是 true 还是 false），取其 id 调用 admin_toggle_template(action="deactivate")
- 只有当遍历完所有 data 条目后确实没有名称匹配项，才可告知用户「未找到该模板」
- list_templates 调用时务必传 limit=20，确保获取全部模板而非默认的前 8 个

【公告管理规则】
|- 用户说「查看公告」「公告列表」「当前有哪些公告」「最近的通知」→ 立即调用 list_announcements，不得凭空回复
|- 用户说「发布公告：...」「创建一条公告」「发一条通知说...」→ 调用 create_announcement，title/content 必须来自用户输入，不得编造
|- 创建公告前必须在 confirm_text 中完整列出标题和正文内容，让管理员确认后再执行
|- expires_at 只有用户明确说「X天后过期」「X日失效」时才传，其余情况不传（即永不过期）
|- is_pinned 只有用户明确说「置顶」时才传 true，默认 false)";
  }

  return prompt;
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

// ── 工具描述构建 ───────────────────────────────────────────────────────────────
nlohmann::json AssistantService::BuildTools(bool is_admin) const {
  nlohmann::json tools = nlohmann::json::array();

  // ── 服务端工具 ─────────────────────────────────────────────────────────────

  tools.push_back({
    {"type", "function"},
    {"function", {
      {"name", "search_ppt_history"},
      {"description", "搜索用户的历史PPT记录，支持关键词和自然语言描述。用户说「找」「搜索」「查询」「帮我看看」历史PPT时使用此工具。"},
      {"parameters", {
        {"type", "object"},
        {"properties", {
          {"query", {{"type", "string"}, {"description", "搜索关键词或自然语言描述，如「关于人工智能的PPT」"}}},
          {"limit", {{"type", "integer"}, {"description", "返回结果数量，默认5，最大10"}, {"default", 5}}}
        }},
        {"required", {"query"}}
      }}
    }}
  });

  tools.push_back({
    {"type", "function"},
    {"function", {
      {"name", "delete_ppt"},
      {"description", "删除用户指定的PPT记录。删除操作不可恢复，必须提供 confirm_text 说明后果。"},
      {"parameters", {
        {"type", "object"},
        {"properties", {
          {"ppt_id", {{"type", "string"}, {"description", "PPT记录的ID"}}},
          {"ppt_title", {{"type", "string"}, {"description", "PPT标题，用于确认文本中展示"}}},
          {"confirm_text", {{"type", "string"}, {"description", "向用户展示的操作确认提示，必须包含「此操作不可恢复」"}}}
        }},
        {"required", {"ppt_id", "ppt_title", "confirm_text"}}
      }}
    }}
  });

  tools.push_back({
    {"type", "function"},
    {"function", {
      {"name", "batch_delete_ppt"},
      {"description",
       "批量删除用户多条PPT记录。适用于「删除我所有关于XX的PPT」「批量清理PPT」等场景。\n"
       "⚠️ 强制流程：\n"
       "1. 必须先调用 search_ppt_history 获取符合条件的PPT列表\n"
       "2. 将列表通过 ppt_list 参数传入，前端展示被删清单后由用户二次确认\n"
       "3. 确认后后端执行批量删除\n"
       "- 操作不可恢复，必须在 confirm_text 中说明后果\n"
       "- 单次删除数量不超过20条，超过需提示用户分批操作"},
      {"parameters", {
        {"type", "object"},
        {"properties", {
          {"ppt_list", {
            {"type", "array"},
            {"description", "待删除的PPT列表，每项数据必须来自 search_ppt_history 工具的真实返回结果，尽量保留所有可用字段"},
            {"items", {
              {"type", "object"},
              {"properties", {
                {"id",            {{"type", "string"}, {"description", "PPT记录的ID"}}},
                {"title",         {{"type", "string"}, {"description", "PPT标题"}}},
                {"topic",         {{"type", "string"}, {"description", "PPT主题描述"}}},
                {"template_name", {{"type", "string"}, {"description", "使用的模板名称"}}},
                {"pages",         {{"type", "integer"}, {"description", "页数"}}},
                {"created_at",    {{"type", "integer"}, {"description", "创建时间（Unix时间戳）"}}},
                {"has_file",      {{"type", "boolean"}, {"description", "是否有可下载文件"}}}
              }},
              {"required", {"id", "title"}}
            }}
          }},
          {"confirm_text", {{"type", "string"}, {"description", "向用户展示的操作确认提示，必须说明将删除的数量和「此操作不可恢复」"}}}
        }},
        {"required", {"ppt_list", "confirm_text"}}
      }}
    }}
  });

  tools.push_back({
    {"type", "function"},
    {"function", {
      {"name", "list_materials"},
      {"description", "列出【当前用户自己】上传的素材文件（PDF、DOCX、TXT等）。仅用于「我的素材」「我上传的文件」场景。"
       "管理员若要查看全量素材或按审核状态筛选，请用 admin_list_materials。"},
      {"parameters", {
        {"type", "object"},
        {"properties", {}},
        {"required", nlohmann::json::array()}
      }}
    }}
  });

  tools.push_back({
    {"type", "function"},
    {"function", {
      {"name", "delete_material"},
      {"description", "删除用户指定的素材文件。删除操作不可恢复，必须在 confirm_text 中说明后果。"},
      {"parameters", {
        {"type", "object"},
        {"properties", {
          {"material_id", {{"type", "string"}, {"description", "素材的ID"}}},
          {"filename", {{"type", "string"}, {"description", "素材文件名，用于确认文本中展示"}}},
          {"confirm_text", {{"type", "string"}, {"description", "向用户展示的操作确认提示，必须包含「此操作不可恢复」"}}}
        }},
        {"required", {"material_id", "filename", "confirm_text"}}
      }}
    }}
  });

  // list_templates 描述根据权限略有不同
  const std::string tmpl_desc = is_admin
      ? "获取PPT模板列表（管理员视角：返回全部模板，包含上架状态标记）。用户问「模板列表」「有哪些模板」「上架了哪些模板」时使用。"
      : "获取当前已上架的PPT模板列表，可根据用户偏好推荐合适模板。用户问「有哪些模板」「推荐个模板」时使用。";

  tools.push_back({
    {"type", "function"},
    {"function", {
      {"name", "list_templates"},
      {"description", tmpl_desc},
      {"parameters", {
        {"type", "object"},
        {"properties", {
          {"limit", {{"type", "integer"}, {"description", "返回数量，默认8，最大20"}, {"default", 8}}}
        }},
        {"required", nlohmann::json::array()}
      }}
    }}
  });

  if (is_admin) {
    tools.push_back({
      {"type", "function"},
      {"function", {
        {"name", "admin_toggle_template"},
        {"description",
         "管理员对指定模板执行上架或下架操作。\n"
         "- action=\"activate\"：上架模板（is_active=false → true），用户可见\n"
         "- action=\"deactivate\"：下架模板（is_active=true → false），用户不可见\n"
         "用户说「上架某模板」「下架某模板」「把这个模板上线/下线」时使用。\n"
         "⚠️ 强制流程：\n"
         "1. 先调 list_templates（limit=20）获取完整模板列表\n"
         "2. 列表中 is_active=false 的模板是「已存在但未上架」，is_active=true 是「已上架」\n"
         "3. 从 data 数组中按名称找到对应记录的真实 id（无论 is_active 是 true 还是 false）\n"
         "4. 用该 id 调用本工具执行上架或下架\n"
         "5. 严禁根据模板名称自行猜测或编造 template_id\n"
         "6. 只有当 list_templates 的完整结果（全部 data 条目）中确实没有该名称，才告知用户未找到"},
        {"parameters", {
          {"type", "object"},
          {"properties", {
            {"template_id", {{"type", "string"}, {"description", "模板的真实ID，必须来自 list_templates 的返回结果"}}},
            {"template_name", {{"type", "string"}, {"description", "模板名称，用于确认文本展示"}}},
            {"action", {{"type", "string"}, {"description", "操作类型：activate（上架）或 deactivate（下架）"}}},
            {"confirm_text", {{"type", "string"}, {"description", "向管理员展示的操作确认提示"}}}
          }},
          {"required", {"template_id", "template_name", "action", "confirm_text"}}
        }}
      }}
    });
  }

  // ── 客户端工具 ─────────────────────────────────────────────────────────────

  tools.push_back({
    {"type", "function"},
    {"function", {
      {"name", "navigate_to_page"},
      {"description", "跳转到系统的指定页面。用户说「去」「打开」「跳转」某个页面时使用。"},
      {"parameters", {
        {"type", "object"},
        {"properties", {
          {"page", {{"type", "string"}, {"description", "目标页面。可选值：history（历史记录）、generate（生成PPT）、materials（我的素材）、templates（模板中心）、profile（个人信息）、dashboard（仪表板）"}}}
        }},
        {"required", {"page"}}
      }}
    }}
  });

  tools.push_back({
    {"type", "function"},
    {"function", {
      {"name", "trigger_generate_ppt"},
      {"description", "跳转到PPT生成页面并自动开始生成流程。用户说「帮我生成」「创建一个PPT」「生成一份关于XX的PPT」时使用。\n若用户给出了主题/标题/页数等信息，请一并传入，系统会自动完成大纲生成和PPT生成，无需用户手动操作。"},
      {"parameters", {
        {"type", "object"},
        {"properties", {
          {"topic",         {{"type", "string"},  {"description", "PPT主题描述，必填，尽量详细"}}},
          {"title",         {{"type", "string"},  {"description", "PPT标题，若用户未指定则根据主题生成一个简洁标题"}}},
          {"page_count",    {{"type", "integer"}, {"description", "页数，默认10，范围5-30"}}},
          {"style",         {{"type", "string"},  {"description", "风格ID，可选值：business/tech/creative/education，可为空"}}},
          {"template_id",   {{"type", "string"},  {"description", "模板ID，若用户指定了模板则填入，否则为空"}}},
          {"generate_mode", {{"type", "string"},  {"description", "生成模式：template（模板）/ style（风格）/ ai_native（AI自由发挥），默认template"}}},
          {"auto_generate", {{"type", "boolean"}, {"description", "是否自动触发生成流程，通常为true，只有用户明确说「我自己来」时才传false"}}}
        }},
        {"required", {"topic"}}
      }}
    }}
  });

  tools.push_back({
    {"type", "function"},
    {"function", {
      {"name", "open_ppt_editor"},
      {"description", "打开指定PPT的在线编辑器。用户说「编辑」「修改」某个PPT时使用。"},
      {"parameters", {
        {"type", "object"},
        {"properties", {
          {"ppt_id", {{"type", "string"}, {"description", "PPT记录的ID"}}},
          {"ppt_title", {{"type", "string"}, {"description", "PPT标题"}}}
        }},
        {"required", {"ppt_id"}}
      }}
    }}
  });

  tools.push_back({
    {"type", "function"},
    {"function", {
      {"name", "download_ppt"},
      {"description", "触发下载指定PPT文件。用户说「下载」某个PPT时使用。"},
      {"parameters", {
        {"type", "object"},
        {"properties", {
          {"ppt_id", {{"type", "string"}, {"description", "PPT记录的ID"}}},
          {"ppt_title", {{"type", "string"}, {"description", "PPT标题"}}}
        }},
        {"required", {"ppt_id"}}
      }}
    }}
  });

  tools.push_back({
    {"type", "function"},
    {"function", {
      {"name", "batch_download_ppt"},
      {"description",
       "触发批量PPT下载：系统自动打包成ZIP并在对话中展示下载卡片。\n"
       "适用于「下载我最近的5个PPT」「批量下载关于XX的PPT」「下载IPPTGen和PASS这两个PPT」等场景。\n"
       "⚠️ 强制流程：\n"
       "1. 必须先调用 search_ppt_history 获取真实列表\n"
       "2. 用户指定了具体标题时，必须在 search_ppt_history 结果中按标题关键词匹配，取匹配到的完整记录\n"
       "3. 用户说「第一个和第二个」时，以当前 search_ppt_history 返回列表的顺序为准，取对应位置的完整记录\n"
       "4. ppt_list 中每项必须包含 search_ppt_history 返回的所有字段（id、title、topic、template_name、pages、created_at、has_file），不得只传 id 和 title\n"
       "5. 严禁猜测、替换或编造任何字段值\n"
       "6. 单次不超过10个"},
      {"parameters", {
        {"type", "object"},
        {"properties", {
          {"ppt_list", {
            {"type", "array"},
            {"description", "待下载的PPT列表，每项数据必须来自 search_ppt_history 工具的真实返回结果，尽量保留所有可用字段"},
            {"items", {
              {"type", "object"},
              {"properties", {
                {"id",            {{"type", "string"}, {"description", "PPT记录的ID"}}},
                {"title",         {{"type", "string"}, {"description", "PPT标题"}}},
                {"topic",         {{"type", "string"}, {"description", "PPT主题描述"}}},
                {"template_name", {{"type", "string"}, {"description", "使用的模板名称"}}},
                {"pages",         {{"type", "integer"}, {"description", "页数"}}},
                {"created_at",    {{"type", "integer"}, {"description", "创建时间（Unix时间戳）"}}},
                {"has_file",      {{"type", "boolean"}, {"description", "是否有可下载文件"}}}
              }},
              {"required", {"id", "title"}}
            }}
          }}
        }},
        {"required", {"ppt_list"}}
      }}
    }}
  });

  tools.push_back({
    {"type", "function"},
    {"function", {
      {"name", "show_material_upload"},
      {"description", "打开素材上传面板，帮用户上传PDF、DOCX或TXT文件。用户说「上传素材」「上传文件」时使用。"},
      {"parameters", {
        {"type", "object"},
        {"properties", {}},
        {"required", nlohmann::json::array()}
      }}
    }}
  });

  tools.push_back({
    {"type", "function"},
    {"function", {
      {"name", "preview_material"},
      {"description", "在管理员素材管理页面打开指定素材的预览抽屉。"
       "用户说「预览这个素材」「查看素材内容」「打开素材」时使用。"
       "必须传入工具调用结果中真实存在的 material_id，严禁编造。"},
      {"parameters", {
        {"type", "object"},
        {"properties", {
          {"material_id", {{"type", "string"}, {"description", "素材的真实ID，必须来自 admin_list_materials 或 list_materials 的返回结果"}}},
          {"filename", {{"type", "string"}, {"description", "素材文件名，用于日志和确认文本"}}}
        }},
        {"required", {"material_id"}}
      }}
    }}
  });

  tools.push_back({
    {"type", "function"},
    {"function", {
      {"name", "fill_login_form"},
      {"description", "协助用户登录：跳转到登录页面。用户未登录时说「登录」「我要登录」时使用。"},
      {"parameters", {
        {"type", "object"},
        {"properties", {
          {"username_hint", {{"type", "string"}, {"description", "可选，预填的用户名提示"}}}
        }},
        {"required", nlohmann::json::array()}
      }}
    }}
  });

  // ── 管理员专属工具 ─────────────────────────────────────────────────────────
  if (is_admin) {
    tools.push_back({
      {"type", "function"},
      {"function", {
        {"name", "get_system_stats"},
        {"description", "获取系统运营统计数据（仅管理员可用）：总用户数、总生成量、成功率等。"},
        {"parameters", {
          {"type", "object"},
          {"properties", {}},
          {"required", nlohmann::json::array()}
        }}
      }}
    });

    tools.push_back({
      {"type", "function"},
      {"function", {
        {"name", "get_insights"},
        {"description",
         "获取系统偏好洞察数据（仅管理员可用）。通过 sections 参数控制返回范围：\n"
         "- 用户只问「热门关键词」→ sections=[\"keywords\"]\n"
         "- 用户只问「常用模板」「模板排行」 → sections=[\"templates\"]\n"
         "- 用户只问「页数分布」 → sections=[\"pages\"]\n"
         "- 用户只问「用户留存」「用户漏斗」 → sections=[\"funnel\"]\n"
         "- 用户要「偏好洞察报告」「完整报告」「偏好分析摘要」「生成偏好分析」→ sections=[\"keywords\",\"templates\",\"pages\",\"funnel\"]\n"
         "必须严格按用户意图选择 sections，不得多传。"},
        {"parameters", {
          {"type", "object"},
          {"properties", {
            {"sections", {
              {"type", "array"},
              {"description", "要返回的数据项，可选值：keywords/templates/pages/funnel，可多选。不传则默认全部"},
              {"items", {{"type", "string"}}},
              {"default", nlohmann::json::array({"keywords", "templates", "pages", "funnel"})}
            }}
          }},
          {"required", nlohmann::json::array()}
        }}
      }}
    });

    tools.push_back({
      {"type", "function"},
      {"function", {
        {"name", "list_users"},
        {"description", "搜索用户列表（仅管理员可用）。"},
        {"parameters", {
          {"type", "object"},
          {"properties", {
            {"query", {{"type", "string"}, {"description", "搜索关键词（用户名或邮箱），可为空"}}}
          }},
          {"required", nlohmann::json::array()}
        }}
      }}
    });

    tools.push_back({
      {"type", "function"},
      {"function", {
        {"name", "admin_list_materials"},
        {"description",
         "管理员查看【全量】素材列表（所有用户的素材），支持筛选和分页。\n"
         "筛选规则（重要）：\n"
         "- 「待审核」「未审核」「需要审核」→ 必须传 review_status=\"unreviewed\"\n"
         "- 「审核通过」「合规」→ review_status=\"pass\"\n"
         "- 「违规」→ review_status=\"violation\"\n"
         "- 「提取失败」→ status=\"failed\"\n"
         "- 不传 review_status 则返回全部（包含已审核和未审核混合结果）\n"
         "注意：查看「我自己的素材」请用 list_materials 工具，不要用本工具。"},
        {"parameters", {
          {"type", "object"},
          {"properties", {
            {"status", {{"type", "string"}, {"description", "按提取状态筛选：pending/extracting/completed/failed，不填返回全部"}}},
            {"review_status", {{"type", "string"}, {"description", "按审核状态筛选。待审核/未审核=unreviewed，通过=pass，违规=violation，不填返回全部"}}},
            {"page", {{"type", "integer"}, {"description", "页码，从1开始，默认1"}, {"default", 1}}},
            {"page_size", {{"type", "integer"}, {"description", "每页条数，默认15，最大20"}, {"default", 15}}}
          }},
          {"required", nlohmann::json::array()}
        }}
      }}
    });

    tools.push_back({
      {"type", "function"},
      {"function", {
        {"name", "admin_review_material"},
        {"description", "管理员对指定素材触发 AI 内容审核，判断是否包含违规内容。审核结果会存储到数据库。用户说「审核素材」「检查这个素材是否合规」时使用。"},
        {"parameters", {
          {"type", "object"},
          {"properties", {
            {"material_id", {{"type", "string"}, {"description", "素材的ID"}}},
            {"filename", {{"type", "string"}, {"description", "素材文件名，用于展示"}}}
          }},
          {"required", {"material_id"}}
        }}
      }}
    });

    tools.push_back({
      {"type", "function"},
      {"function", {
        {"name", "admin_delete_material"},
        {"description", "管理员强制删除任意用户的素材（不限归属），并向所属用户发送删除通知。用于处理违规素材。删除不可恢复，必须在 confirm_text 中说明原因。"},
        {"parameters", {
          {"type", "object"},
          {"properties", {
            {"material_id", {{"type", "string"}, {"description", "素材的ID"}}},
            {"filename", {{"type", "string"}, {"description", "素材文件名，用于确认文本展示"}}},
            {"delete_reason", {{"type", "string"}, {"description", "删除原因，将通知给素材所属用户"}}},
            {"confirm_text", {{"type", "string"}, {"description", "向管理员展示的确认提示，必须包含「此操作不可恢复」"}}}
          }},
          {"required", {"material_id", "filename", "delete_reason", "confirm_text"}}
        }}
      }}
    });

    // ── 公告管理 ────────────────────────────────────────────────────────────
    tools.push_back({
      {"type", "function"},
      {"function", {
        {"name", "list_announcements"},
        {"description",
         "查询公告列表（仅管理员可用）。返回系统全量公告，包含已过期和未来生效的公告。\n"
         "用户说「查看公告」「当前有哪些公告」「公告列表」「最近发布的公告」时使用。\n"
         "普通用户查看公告请通过 show_announcement 工具。"},
        {"parameters", {
          {"type", "object"},
          {"properties", {
            {"page",      {{"type", "integer"}, {"description", "页码，从1开始，默认1"}, {"default", 1}}},
            {"page_size", {{"type", "integer"}, {"description", "每页条数，默认10，最大20"}, {"default", 10}}}
          }},
          {"required", nlohmann::json::array()}
        }}
      }}
    });

    tools.push_back({
      {"type", "function"},
      {"function", {
        {"name", "create_announcement"},
        {"description",
         "创建并发布一条新公告（仅管理员可用）。\n"
         "用户说「发布公告」「创建公告」「发一条通知：...」时使用。\n"
         "title 为公告标题，content 为正文内容；is_pinned 为置顶（默认 false）；"
         "expires_at 为过期时间（ISO 8601，不填则永不过期）。\n"
         "创建前必须在 confirm_text 中告知公告内容摘要，让管理员确认无误后再发布。"},
        {"parameters", {
          {"type", "object"},
          {"properties", {
            {"title",      {{"type", "string"}, {"description", "公告标题，不能为空"}}},
            {"content",    {{"type", "string"}, {"description", "公告正文内容，不能为空"}}},
            {"is_pinned",  {{"type", "boolean"}, {"description", "是否置顶，默认 false"}, {"default", false}}},
            {"expires_at", {{"type", "string"}, {"description", "过期时间（ISO 8601），不填则永不过期"}}},
            {"confirm_text", {{"type", "string"}, {"description", "确认提示，须说明标题和正文内容摘要，让管理员核对后确认发布"}}}
          }},
          {"required", {"title", "content", "confirm_text"}}
        }}
      }}
    });

    tools.push_back({
      {"type", "function"},
      {"function", {
        {"name", "toggle_maintenance_mode"},
        {"description",
         "开启或关闭系统维护模式（仅管理员可用）。"
         "【重要】用户说「开启维护模式」「进入维护」「关闭维护模式」「结束维护」「恢复系统」时，"
         "必须立即触发此 function call，绝不能只用文字描述。"
         "confirm_code 固定传空字符串 \"\"（前端自动处理确认流程），confirm_text 写明操作后果。"},
        {"parameters", {
          {"type", "object"},
          {"properties", {
            {"action", {
              {"type", "string"},
              {"description", "enable=开启维护模式，disable=关闭维护模式"},
              {"enum", {"enable", "disable"}}
            }},
            {"reason", {
              {"type", "string"},
              {"description", "维护原因，可选"}
            }},
            {"confirm_code", {
              {"type", "string"},
              {"description", "始终传空字符串 \"\"，前端负责弹出用户输入框"}
            }},
            {"confirm_text", {
              {"type", "string"},
              {"description", "操作确认提示文本，须说明后果"}
            }}
          }},
          {"required", {"action", "confirm_text"}}
        }}
      }}
    });
  }

  return tools;
}

// ── 客户端/危险工具判断 ────────────────────────────────────────────────────────
bool AssistantService::IsClientTool(const std::string& tool_name) {
  return kClientTools.count(tool_name) > 0;
}

bool AssistantService::ToolRequiresConfirm(const std::string& tool_name) {
  return kDangerTools.count(tool_name) > 0;
}

std::string AssistantService::BuildConfirmText(const std::string& tool_name,
                                                const nlohmann::json& params) {
  auto get_str = [&](const char* key) -> std::string {
    if (params.contains(key) && params[key].is_string()) return params[key].get<std::string>();
    return {};
  };

  if (tool_name == "delete_ppt") {
    const std::string title = get_str("ppt_title");
    // 优先使用工具参数中的 confirm_text
    const std::string ct = get_str("confirm_text");
    if (!ct.empty()) return ct;
    return "确定要删除 PPT「" + title + "」吗？此操作不可恢复。";
  }
  if (tool_name == "batch_delete_ppt") {
    const std::string ct = get_str("confirm_text");
    if (!ct.empty()) return ct;
    // 从 ppt_list 构造确认文本
    if (params.contains("ppt_list") && params["ppt_list"].is_array()) {
      const int count = static_cast<int>(params["ppt_list"].size());
      return "确定要批量删除以下 " + std::to_string(count) + " 个 PPT 吗？此操作不可恢复。";
    }
    return "确定要批量删除这些 PPT 吗？此操作不可恢复。";
  }
  if (tool_name == "delete_material") {
    const std::string name = get_str("filename");
    return "确定要删除素材「" + name + "」吗？此操作不可恢复。";
  }
  if (tool_name == "admin_delete_material") {
    const std::string name = get_str("filename");
    const std::string reason = get_str("delete_reason");
    const std::string ct = get_str("confirm_text");
    if (!ct.empty()) return ct;
    return "【管理员操作】确定要强制删除素材「" + name + "」吗？删除原因：" + reason + "。此操作不可恢复，且会通知素材所属用户。";
  }
  if (tool_name == "admin_toggle_template") {
    const std::string name = get_str("template_name");
    const std::string action = get_str("action");
    const std::string ct = get_str("confirm_text");
    if (!ct.empty()) return ct;
    if (action == "activate") {
      return "【管理员操作】确定要将模板「" + name + "」上架吗？上架后所有用户可见并使用该模板。";
    } else {
      return "【管理员操作】确定要将模板「" + name + "」下架吗？下架后用户将无法看到或使用该模板。";
    }
  }
  if (tool_name == "create_announcement") {
    const std::string ann_title   = get_str("title");
    const std::string ann_content = get_str("content");
    const std::string ct          = get_str("confirm_text");
    if (!ct.empty()) return ct;
    const std::string preview = ann_content.size() > 60
                                  ? ann_content.substr(0, 60) + "…"
                                  : ann_content;
    return "【管理员操作】确认发布以下公告吗？\n\n标题：" + ann_title +
           "\n内容：" + preview +
           "\n\n公告发布后即时对所有已登录用户可见。";
  }
  if (tool_name == "toggle_maintenance_mode") {
    const std::string action = get_str("action");
    const std::string reason = get_str("reason");
    const std::string ct = get_str("confirm_text");
    if (!ct.empty()) return ct;
    if (action == "enable") {
      return "【最高级危险操作】确定要开启系统维护模式吗？\n\n开启后所有普通用户将立即无法访问系统，直到您手动关闭维护模式为止。\n\n维护原因：" + (reason.empty() ? "（未填写）" : reason) + "\n\n请在下方输入 CONFIRM 以确认执行。";
    } else {
      return "【管理员操作】确定要关闭系统维护模式，恢复系统正常访问吗？\n\n关闭后所有用户将恢复正常使用。\n\n请在下方输入 CONFIRM 以确认执行。";
    }
  }
  return "确定执行此操作吗？";
}

// ── 服务端工具执行 ─────────────────────────────────────────────────────────────
nlohmann::json AssistantService::ExecuteServerTool(const std::string& tool_name,
                                                     const nlohmann::json& params,
                                                     std::uint64_t user_id,
                                                     bool is_admin) const {
  auto get_str = [&](const char* key) -> std::string {
    if (params.contains(key) && params[key].is_string()) return params[key].get<std::string>();
    return {};
  };
  auto get_int = [&](const char* key, int def) -> int {
    if (params.contains(key) && params[key].is_number_integer())
      return params[key].get<int>();
    return def;
  };

  // ── search_ppt_history ────────────────────────────────────────────────────
  if (tool_name == "search_ppt_history") {
    const std::string query = get_str("query");
    const int limit = std::min(get_int("limit", 5), 10);

    nlohmann::json items = nlohmann::json::array();
    bool used_vector = false;

    // 优先使用向量语义检索
    if (ai_search_service_ && !query.empty()) {
      try {
        auto resp = ai_search_service_->Search(query, user_id, limit, false);
        used_vector = !resp.fallback;

        if (!resp.results.empty() && ppt_service_) {
          // 批量从 DB 补全 output_path（Qdrant payload 不存储此字段）
          std::vector<std::uint64_t> ids;
          ids.reserve(resp.results.size());
          for (const auto& r : resp.results) ids.push_back(r.ppt_id);

          std::string db_err;
          auto db_rows = ppt_service_->GetRequestsByIds(user_id, ids, db_err);
          // 建立 id → output_path 快速查找表
          std::unordered_map<std::uint64_t, std::string> path_map;
          for (const auto& row : db_rows) path_map[row.id] = row.output_path;

          for (const auto& r : resp.results) {
            auto it = path_map.find(r.ppt_id);
            const bool has_file = (it != path_map.end()) && !it->second.empty();
            nlohmann::json item = {
                {"id", std::to_string(r.ppt_id)},
                {"title", r.title.empty() ? r.topic : r.title},
                {"topic", r.topic},
                {"template_name", r.template_name},
                {"pages", r.pages},
                {"status", r.status},
                {"created_at", r.created_at},
                {"score", r.score},
                {"has_file", has_file}
            };
            if (has_file) {
              item["download_url"] = "/api/ppt/file?id=" + std::to_string(r.ppt_id);
            }
            if (!r.reason.empty()) item["reason"] = r.reason;
            items.push_back(std::move(item));
          }
        }
      } catch (const std::exception& e) {
        Logger::Warn(std::string("AssistantService search_ppt_history AI 检索异常: ") + e.what());
      }
    }

    // 降级：使用 SQL LIKE 检索
    if (items.empty() && ppt_service_) {
      std::string err;
      auto history = ppt_service_->GetHistory(user_id, query, err);
      if (static_cast<int>(history.size()) > limit) history.resize(limit);
      for (const auto& h : history) {
        const bool has_file = !h.output_path.empty();
        nlohmann::json item = {
            {"id", std::to_string(h.id)},
            {"title", h.title.empty() ? h.topic : h.title},
            {"topic", h.topic},
            {"template_name", h.template_name},
            {"pages", h.pages},
            {"status", h.status},
            {"created_at", h.created_at},
            {"has_file", has_file}
        };
        if (has_file) {
          item["download_url"] = "/api/ppt/file?id=" + std::to_string(h.id);
        }
        items.push_back(std::move(item));
      }
    }

    if (items.empty()) {
      return {{"card_type", "text"}, {"data", "没有找到相关的 PPT 记录。"}};
    }
    return {
        {"card_type", "ppt_list"},
        {"data", items},
        {"total", static_cast<int>(items.size())},
        {"search_mode", used_vector ? "vector" : "keyword"}
    };
  }

  // ── delete_ppt ────────────────────────────────────────────────────────────
  if (tool_name == "delete_ppt") {
    const std::string ppt_id_str = get_str("ppt_id");
    if (ppt_id_str.empty() || !ppt_service_) {
      return {{"card_type", "text"}, {"data", "缺少 ppt_id 或服务不可用"}};
    }
    std::uint64_t ppt_id = 0;
    try { ppt_id = std::stoull(ppt_id_str); } catch (...) {
      return {{"card_type", "text"}, {"data", "ppt_id 格式无效"}};
    }
    std::string err;
    const bool ok = ppt_service_->DeleteRequest(user_id, ppt_id, err);
    if (ok) {
      return {{"card_type", "text"}, {"data", "PPT 已成功删除。"}, {"success", true}};
    } else {
      return {{"card_type", "text"}, {"data", "删除失败：" + err}, {"success", false}};
    }
  }

  // ── batch_delete_ppt ──────────────────────────────────────────────────────
  if (tool_name == "batch_delete_ppt") {
    if (!ppt_service_) {
      return {{"card_type", "text"}, {"data", "PPT 服务暂不可用"}};
    }
    if (!params.contains("ppt_list") || !params["ppt_list"].is_array() ||
        params["ppt_list"].empty()) {
      return {{"card_type", "text"}, {"data", "缺少待删除的 PPT 列表（ppt_list）"}};
    }

    const auto& ppt_list = params["ppt_list"];
    const int max_batch = 20;
    if (static_cast<int>(ppt_list.size()) > max_batch) {
      return {
          {"card_type", "text"},
          {"data", "单次批量删除不超过 " + std::to_string(max_batch) + " 条，请分批操作。"},
          {"success", false}
      };
    }

    // 收集 ID 列表
    std::vector<std::uint64_t> ids;
    std::vector<std::string> titles;
    for (const auto& item : ppt_list) {
      if (!item.contains("id")) continue;
      std::string id_str;
      if (item["id"].is_string()) id_str = item["id"].get<std::string>();
      else if (item["id"].is_number()) id_str = std::to_string(item["id"].get<std::uint64_t>());
      try {
        ids.push_back(std::stoull(id_str));
        titles.push_back(item.value("title", id_str));
      } catch (...) {}
    }

    if (ids.empty()) {
      return {{"card_type", "text"}, {"data", "ppt_list 中没有有效的 ID"}, {"success", false}};
    }

    std::vector<std::uint64_t> failed_ids;
    std::string batch_err;
    const int deleted = ppt_service_->BatchDeleteRequests(user_id, ids, failed_ids, batch_err);

    // 构建结果摘要
    nlohmann::json deleted_list = nlohmann::json::array();
    nlohmann::json failed_list  = nlohmann::json::array();
    std::set<std::uint64_t> failed_set(failed_ids.begin(), failed_ids.end());
    for (size_t i = 0; i < ids.size(); ++i) {
      nlohmann::json entry = {{"id", std::to_string(ids[i])}, {"title", titles[i]}};
      if (failed_set.count(ids[i])) failed_list.push_back(entry);
      else                          deleted_list.push_back(entry);
    }

    nlohmann::json result = {
        {"card_type", "batch_delete_result"},
        {"deleted_count", deleted},
        {"failed_count",  static_cast<int>(failed_ids.size())},
        {"deleted_list",  deleted_list},
        {"failed_list",   failed_list},
        {"success",       failed_ids.empty()}
    };
    if (!batch_err.empty()) result["error"] = batch_err;
    return result;
  }

  // ── list_materials ────────────────────────────────────────────────────────
  if (tool_name == "list_materials") {
    if (!material_service_) {
      return {{"card_type", "text"}, {"data", "素材服务暂不可用"}};
    }
    std::string err;
    auto mats = material_service_->ListMaterials(user_id, err);

    nlohmann::json items = nlohmann::json::array();
    for (const auto& m : mats) {
      std::string review_status = "unreviewed";
      if (!m.review_result.empty()) {
        try {
          auto rj = nlohmann::json::parse(m.review_result);
          review_status = rj.value("result", "unreviewed");
        } catch (...) {}
      }
      items.push_back({
          {"id", m.id},
          {"filename", m.filename},
          {"file_type", m.file_type},
          {"file_size", m.file_size},
          {"status", m.status},
          {"review_status", review_status},
          {"created_at", m.created_at}
      });
    }
    return {{"card_type", "my_materials"}, {"data", items}, {"total", static_cast<int>(items.size())}};
  }

  // ── delete_material ───────────────────────────────────────────────────────
  if (tool_name == "delete_material") {
    const std::string material_id = get_str("material_id");
    if (material_id.empty() || !material_service_) {
      return {{"card_type", "text"}, {"data", "缺少 material_id 或素材服务不可用"}};
    }
    std::string err;
    const bool ok = material_service_->DeleteMaterial(material_id, user_id, err);
    if (ok) {
      return {{"card_type", "text"}, {"data", "素材已成功删除。"}, {"success", true}};
    } else {
      return {{"card_type", "text"}, {"data", "删除失败：" + err}, {"success", false}};
    }
  }

  // ── admin_list_materials ──────────────────────────────────────────────────
  if (tool_name == "admin_list_materials") {
    if (!is_admin) {
      return {{"card_type", "text"}, {"data", "权限不足：仅管理员可查看全量素材"}};
    }
    if (!material_service_) {
      return {{"card_type", "text"}, {"data", "素材服务暂不可用"}};
    }
    const std::string status_filter = get_str("status");
    const std::string review_status_filter = get_str("review_status");
    const int page = std::max(get_int("page", 1), 1);
    const int page_size = std::min(std::max(get_int("page_size", 15), 1), 20);

    MaterialService::AdminMaterialFilter filter;
    filter.status = status_filter;
    filter.review_status = review_status_filter;
    filter.page = page;
    filter.page_size = page_size;

    std::string err;
    int total = 0;
    auto mats = material_service_->AdminListMaterials(filter, total, err);


    nlohmann::json items = nlohmann::json::array();
    for (const auto& m : mats) {
      // 解析 review_result JSON
      std::string review_status = "unreviewed";
      std::string review_reason;
      if (!m.review_result.empty()) {
        try {
          auto rj = nlohmann::json::parse(m.review_result);
          review_status = rj.value("result", "unreviewed");
          review_reason = rj.value("reason", "");
        } catch (...) {}
      }
      items.push_back({
          {"id", m.id},
          {"filename", m.filename},
          {"file_type", m.file_type},
          {"file_size", m.file_size},
          {"status", m.status},
          {"user_id", m.user_id},
          {"review_status", review_status},
          {"review_reason", review_reason},
          {"created_at", m.created_at}
      });
    }
    return {
        {"card_type", "material_list"},
        {"data", items},
        {"total", total},
        {"page", page},
        {"page_size", page_size},
        {"is_admin_view", true}
    };
  }

  // ── admin_review_material ─────────────────────────────────────────────────
  if (tool_name == "admin_review_material") {
    if (!is_admin) {
      return {{"card_type", "text"}, {"data", "权限不足：仅管理员可执行内容审核"}};
    }
    if (!material_service_) {
      return {{"card_type", "text"}, {"data", "素材服务暂不可用"}};
    }
    const std::string material_id = get_str("material_id");
    if (material_id.empty()) {
      return {{"card_type", "text"}, {"data", "缺少 material_id"}};
    }
    std::string err;
    MaterialService::ReviewResult review;
    const bool ok = material_service_->AdminReviewMaterial(
        material_id, api_key_, timeout_seconds_, review, err);
    if (!ok) {
      return {{"card_type", "text"}, {"data", "审核失败：" + err}, {"success", false}};
    }
    const std::string result_label =
        review.result == "pass" ? "✅ 合规" :
        review.result == "violation" ? "❌ 违规" : "⚠️ 未知";
    return {
        {"card_type", "text"},
        {"data", "审核完成：" + result_label + "\n原因：" + review.reason},
        {"success", true},
        {"review_result", review.result},
        {"review_reason", review.reason}
    };
  }

  // ── admin_delete_material ─────────────────────────────────────────────────
  if (tool_name == "admin_delete_material") {
    if (!is_admin) {
      return {{"card_type", "text"}, {"data", "权限不足：仅管理员可执行此操作"}};
    }
    if (!material_service_) {
      return {{"card_type", "text"}, {"data", "素材服务暂不可用"}};
    }
    const std::string material_id = get_str("material_id");
    const std::string delete_reason = get_str("delete_reason");
    if (material_id.empty()) {
      return {{"card_type", "text"}, {"data", "缺少 material_id"}};
    }
    std::string err;
    const bool ok = material_service_->AdminDeleteMaterial(
        material_id, delete_reason, "admin", err);
    if (ok) {
      return {{"card_type", "text"}, {"data", "素材已强制删除，已通知所属用户。"}, {"success", true}};
    } else {
      return {{"card_type", "text"}, {"data", "删除失败：" + err}, {"success", false}};
    }
  }

  // ── list_templates ────────────────────────────────────────────────────────
  if (tool_name == "list_templates") {
    const int limit = std::min(get_int("limit", 8), 20);

    if (!template_service_) {
      return {{"card_type", "text"}, {"data", "模板服务暂不可用"}};
    }

    auto all_templates = template_service_->GetAll();

    if (is_admin) {
      // 管理员：返回全部模板，并附加上架状态标记
      // 先获取已上架的 id 集合
      std::unordered_set<std::string> active_ids_set;
      if (tmpl_mgr_service_) {
        std::vector<std::string> active_ids;
        std::string err;
        if (tmpl_mgr_service_->ListActiveIds(active_ids, err)) {
          active_ids_set.insert(active_ids.begin(), active_ids.end());
        }
      }

      if (static_cast<int>(all_templates.size()) > limit) {
        all_templates.resize(limit);
      }

      nlohmann::json items = nlohmann::json::array();
      for (const auto& t : all_templates) {
        const bool is_active = active_ids_set.count(t.id) > 0;
        nlohmann::json item = {
            {"id", t.id},
            {"name", t.name},
            {"description", t.description},
            {"preview_image", t.preview_image},
            {"tags", t.tags},
            {"provider", t.provider},
            {"is_active", is_active}
        };
        items.push_back(std::move(item));
      }
      return {
          {"card_type", "template_list"},
          {"data", items},
          {"total", static_cast<int>(items.size())},
          {"admin_view", true}
      };

    } else {
      // 普通用户：只返回已上架且在有效期内的模板
      if (!tmpl_mgr_service_) {
        // 无管理服务则返回全部（降级）
        if (static_cast<int>(all_templates.size()) > limit) {
          all_templates.resize(limit);
        }
      } else {
        std::vector<std::string> active_ids;
        std::string err;
        if (tmpl_mgr_service_->ListActiveIds(active_ids, err)) {
          std::unordered_set<std::string> active_set(active_ids.begin(), active_ids.end());
          std::vector<RemoteTemplate> filtered;
          for (auto& t : all_templates) {
            if (active_set.count(t.id)) {
              filtered.push_back(std::move(t));
            }
          }
          all_templates = std::move(filtered);
        }
        if (static_cast<int>(all_templates.size()) > limit) {
          all_templates.resize(limit);
        }
      }

      nlohmann::json items = nlohmann::json::array();
      for (const auto& t : all_templates) {
        items.push_back({
            {"id", t.id},
            {"name", t.name},
            {"description", t.description},
            {"preview_image", t.preview_image},
            {"tags", t.tags},
            {"provider", t.provider}
        });
      }
      return {
          {"card_type", "template_list"},
          {"data", items},
          {"total", static_cast<int>(items.size())}
      };
    }
  }

  // ── get_system_stats（管理员）─────────────────────────────────────────────
  if (tool_name == "get_system_stats") {
    if (!is_admin) {
      return {{"card_type", "text"}, {"data", "权限不足：仅管理员可查看系统统计"}};
    }
    if (!ppt_service_) {
      return {{"card_type", "text"}, {"data", "统计服务暂不可用"}};
    }
    std::string err;
    PptService::AdminMetrics metrics;
    ppt_service_->GetAdminMetrics("week", metrics, err);
    nlohmann::json stats = {
        {"total", metrics.total},
        {"success", metrics.success},
        {"failed", metrics.failed},
        {"success_rate", metrics.success_rate},
        {"unique_users", metrics.unique_users}
    };
    return {{"card_type", "user_stats"}, {"data", stats}};
  }

  // ── get_insights（管理员）────────────────────────────────────────────────
  if (tool_name == "get_insights") {
    if (!is_admin) {
      return {{"card_type", "text"}, {"data", "权限不足：仅管理员可查看偏好洞察"}};
    }
    if (!ppt_service_) {
      return {{"card_type", "text"}, {"data", "洞察服务暂不可用"}};
    }

    // 解析 sections 参数，确定需要返回哪些数据
    std::unordered_set<std::string> sections_set;
    if (params.contains("sections") && params["sections"].is_array()) {
      for (const auto& s : params["sections"]) {
        if (s.is_string()) sections_set.insert(s.get<std::string>());
      }
    }
    // 未传 sections 则默认全部
    const bool all = sections_set.empty();
    auto want = [&](const std::string& s) { return all || sections_set.count(s) > 0; };

    std::string err;
    PptService::InsightData insight;
    ppt_service_->GetInsights(insight, err);

    nlohmann::json data = nlohmann::json::object();
    // 记录实际返回了哪些 sections，供前端按需渲染
    nlohmann::json returned_sections = nlohmann::json::array();

    if (want("keywords")) {
      nlohmann::json topics_arr = nlohmann::json::array();

      // 优先用 LLM 从 topic 列表中提取语义关键词（与管理员端偏好洞察词云同样的方式）
      bool kw_extracted = false;
      if (!api_key_.empty()) {
        std::vector<std::string> raw_topics;
        std::string te;
        if (ppt_service_->GetAllTopics(raw_topics, 200, te) && !raw_topics.empty()) {
          std::ostringstream topic_list;
          for (size_t i = 0; i < std::min(raw_topics.size(), static_cast<size_t>(200)); ++i) {
            if (!raw_topics[i].empty()) topic_list << raw_topics[i] << "\n";
          }
          const std::string kw_prompt =
              "你是一名数据分析专家。以下是用户生成PPT时填写的主题列表（每行一条）：\n" +
              topic_list.str() +
              "\n请从这些主题中提取高频关键词（名词、短语或概念，2-8字），"
              "统计每个关键词在所有主题中出现或相关的频次，输出前40个高频关键词。"
              "要求：1）合并同义词（如\"人工智能\"和\"AI\"算同一词）；"
              "2）忽略虚词、助词、连词等无意义词汇；"
              "3）输出严格的JSON数组，每个元素包含keyword（字符串）和count（整数）两个字段；"
              "4）按count从大到小排序；5）禁止输出除JSON以外的任何字符。";
          std::string kw_reply, kw_err;
          nlohmann::json msgs = nlohmann::json::array();
          msgs.push_back({{"role", "user"}, {"content", kw_prompt}});
          nlohmann::json dummy_tool_calls;
          if (CallQwenAPIWithTools(msgs, nlohmann::json::array(), kw_reply, dummy_tool_calls, kw_err) && !kw_reply.empty()) {
            try {
              auto arr = nlohmann::json::parse(ExtractJsonArrayFromText(kw_reply));
              if (arr.is_array() && !arr.empty()) {
                for (const auto& item : arr) {
                  if (!item.is_object()) continue;
                  const std::string kw = item.value("keyword", "");
                  const int cnt = item.value("count", 0);
                  if (!kw.empty() && cnt > 0)
                    topics_arr.push_back({{"keyword", kw}, {"count", cnt}});
                }
                kw_extracted = !topics_arr.empty();
              }
            } catch (...) {
              Logger::Warn("get_insights: LLM keyword JSON parse failed, falling back");
            }
          } else {
            Logger::Warn("get_insights: LLM keyword extraction failed: " + kw_err);
          }
        }
      }

      // 回退：使用 GetInsights 中已计算好的简单拆分结果
      if (!kw_extracted) {
        for (const auto& tk : insight.top_topics) {
          topics_arr.push_back({{"keyword", tk.keyword}, {"count", tk.count}});
        }
      }

      data["top_topics"] = topics_arr;
      returned_sections.push_back("keywords");
    }

    if (want("templates")) {
      // 常用模板排行（来自 GetAdminMetrics，覆盖全量历史）
      PptService::AdminMetrics metrics;
      ppt_service_->GetAdminMetrics("month", metrics, err);
      nlohmann::json templates_arr = nlohmann::json::array();
      for (size_t i = 0; i < metrics.template_labels.size(); ++i) {
        int cnt = (i < metrics.template_values.size()) ? metrics.template_values[i] : 0;
        templates_arr.push_back({{"name", metrics.template_labels[i]}, {"count", cnt}});
      }
      data["top_templates"] = templates_arr;
      returned_sections.push_back("templates");
    }

    if (want("pages")) {
      nlohmann::json pages_arr = nlohmann::json::array();
      for (size_t i = 0; i < insight.pages_labels.size(); ++i) {
        int cnt = (i < insight.pages_values.size()) ? insight.pages_values[i] : 0;
        pages_arr.push_back({{"label", insight.pages_labels[i]}, {"count", cnt}});
      }
      data["pages_dist"] = pages_arr;
      returned_sections.push_back("pages");
    }

    if (want("funnel")) {
      data["funnel"] = {
          {"registered",      insight.funnel_registered},
          {"generated_once",  insight.funnel_generated_once},
          {"generated_multi", insight.funnel_generated_multi}
      };
      returned_sections.push_back("funnel");
    }

    return {{"card_type", "insights"}, {"data", data}, {"sections", returned_sections}};
  }

  // ── admin_toggle_template（管理员）────────────────────────────────────────
  if (tool_name == "admin_toggle_template") {
    if (!is_admin) {
      return {{"card_type", "text"}, {"data", "权限不足：仅管理员可操作模板上架状态"}};
    }
    if (!tmpl_mgr_service_) {
      return {{"card_type", "text"}, {"data", "模板管理服务暂不可用"}};
    }
    const std::string template_id = get_str("template_id");
    const std::string template_name = get_str("template_name");
    const std::string action = get_str("action");
    if (template_id.empty()) {
      return {{"card_type", "text"}, {"data", "缺少 template_id"}};
    }
    // 操作完成后内联刷新模板列表，返回最新状态卡片
    auto build_refreshed_list = [&](const std::string& op_msg) -> nlohmann::json {
      nlohmann::json result;
      result["card_type"] = "template_list";
      result["op_message"] = op_msg;
      result["success"] = true;
      result["admin_view"] = true;
      if (!template_service_) {
        result["data"] = nlohmann::json::array();
        return result;
      }
      auto all_tpls = template_service_->GetAll();
      std::unordered_set<std::string> active_set;
      if (tmpl_mgr_service_) {
        std::vector<std::string> aids; std::string e2;
        if (tmpl_mgr_service_->ListActiveIds(aids, e2))
          active_set.insert(aids.begin(), aids.end());
      }
      nlohmann::json items = nlohmann::json::array();
      const int list_limit = 20;
      int cnt = 0;
      for (const auto& t : all_tpls) {
        if (cnt++ >= list_limit) break;
        items.push_back({
            {"id", t.id}, {"name", t.name},
            {"description", t.description},
            {"preview_image", t.preview_image},
            {"tags", t.tags}, {"provider", t.provider},
            {"is_active", active_set.count(t.id) > 0}
        });
      }
      result["data"] = std::move(items);
      result["total"] = cnt;
      return result;
    };

    std::string err;
    if (action == "activate") {
      TemplateEntry entry;
      entry.template_id = template_id;
      entry.template_name = template_name;
      entry.is_active = true;
      entry.created_by = user_id;
      const bool ok = tmpl_mgr_service_->Upsert(entry, err);
      if (ok) {
        return build_refreshed_list("模板「" + template_name + "」已成功上架，用户现在可以使用该模板。");
      } else {
        return {{"card_type", "text"}, {"data", "上架失败：" + err}, {"success", false}};
      }
    } else if (action == "deactivate") {
      const bool ok = tmpl_mgr_service_->Deactivate(template_id, err);
      if (ok) {
        return build_refreshed_list("模板「" + template_name + "」已成功下架，用户将无法看到该模板。");
      } else {
        return {{"card_type", "text"}, {"data", "下架失败：" + err}, {"success", false}};
      }
    } else {
      return {{"card_type", "text"}, {"data", "action 参数无效，应为 activate 或 deactivate"}};
    }
  }

  // ── list_users（管理员）───────────────────────────────────────────────────
  if (tool_name == "list_users") {
    if (!is_admin) {
      return {{"card_type", "text"}, {"data", "权限不足：仅管理员可查看用户列表"}};
    }
    // 管理员用户列表暂时返回提示（需要 AuthService 支持，P3 完善）
    return {{"card_type", "text"}, {"data", "用户管理功能请前往管理员后台操作。"}};
  }

  // ── toggle_maintenance_mode（管理员，最高级确认）────────────────────────────
  if (tool_name == "toggle_maintenance_mode") {
    if (!is_admin) {
      return {{"card_type", "text"}, {"data", "权限不足：仅管理员可操作维护模式"}};
    }
    if (!pool_) {
      return {{"card_type", "text"}, {"data", "数据库连接不可用，无法操作维护模式"}};
    }
    const std::string action       = get_str("action");
    const std::string confirm_code = get_str("confirm_code");
    const std::string reason       = get_str("reason");

    if (action != "enable" && action != "disable") {
      return {{"card_type", "text"}, {"data", "action 参数无效，应为 enable 或 disable"}};
    }

    // 后端再次校验 CONFIRM 字符串（双重保险，前端已校验一次）
    if (confirm_code != "CONFIRM") {
      return {
          {"card_type", "maintenance_confirm_required"},
          {"data", "请在确认框中输入 CONFIRM 以执行此操作"},
          {"action", action},
          {"reason", reason}
      };
    }

    const bool enable = (action == "enable");
    const bool ok = SettingsReader::SetBool(*pool_, "maintenance_mode", enable);
    if (!ok) {
      return {{"card_type", "text"}, {"data", "维护模式操作失败，请检查数据库连接或 system_settings 表是否已初始化（先访问管理员设置页面）"}};
    }

    Logger::Info("AssistantService: toggle_maintenance_mode action=" + action +
                 " user_id=" + std::to_string(user_id) +
                 " reason=" + reason);

    const std::string msg = enable
        ? "✅ 系统维护模式已开启。所有普通用户现在无法访问系统。维护原因：" +
          (reason.empty() ? "（未填写）" : reason) +
          "。请完成维护后及时关闭维护模式。"
        : "✅ 系统维护模式已关闭，系统已恢复正常访问。";

    return {
        {"card_type", "maintenance_result"},
        {"data", msg},
        {"enabled", enable},
        {"success", true}
    };
  }

  // ── list_announcements（管理员）──────────────────────────────────────────
  if (tool_name == "list_announcements") {
    if (!is_admin) {
      return {{"card_type", "text"}, {"data", "权限不足：仅管理员可查看全量公告列表"}};
    }
    if (!pool_) {
      return {{"card_type", "text"}, {"data", "数据库连接不可用"}};
    }

    int page = 1, page_size = 10;
    if (params.contains("page") && params["page"].is_number_integer())
      page = params["page"].get<int>();
    if (params.contains("page_size") && params["page_size"].is_number_integer())
      page_size = params["page_size"].get<int>();
    if (page < 1) page = 1;
    if (page_size < 1 || page_size > 20) page_size = 10;
    int offset = (page - 1) * page_size;

    auto conn_guard = pool_->GetConnection();
    MYSQL* conn = conn_guard.Get();
    if (!conn) {
      return {{"card_type", "text"}, {"data", "数据库连接失败"}};
    }

    // 查询总数
    std::uint64_t total = 0;
    if (mysql_query(conn, "SELECT COUNT(*) FROM announcements") == 0) {
      MYSQL_RES* r = mysql_store_result(conn);
      if (r) {
        MYSQL_ROW row = mysql_fetch_row(r);
        if (row && row[0]) total = std::stoull(row[0]);
        mysql_free_result(r);
      }
    }

    std::ostringstream q;
    q << "SELECT id, title, content, is_pinned, "
      << "UNIX_TIMESTAMP(starts_at), "
      << "IF(expires_at IS NULL, 0, UNIX_TIMESTAMP(expires_at)), "
      << "created_by, UNIX_TIMESTAMP(created_at), UNIX_TIMESTAMP(updated_at) "
      << "FROM announcements "
      << "ORDER BY is_pinned DESC, created_at DESC "
      << "LIMIT " << page_size << " OFFSET " << offset;

    if (mysql_query(conn, q.str().c_str()) != 0) {
      return {{"card_type", "text"}, {"data", std::string("查询失败：") + mysql_error(conn)}};
    }
    MYSQL_RES* res = mysql_store_result(conn);
    if (!res) {
      return {{"card_type", "text"}, {"data", "查询失败：无结果集"}};
    }

    nlohmann::json items = nlohmann::json::array();
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
      nlohmann::json a;
      if (row[0]) a["id"]         = std::stoull(row[0]);
      if (row[1]) a["title"]      = row[1];
      if (row[2]) a["content"]    = row[2];
      if (row[3]) a["is_pinned"]  = std::stoi(row[3]) != 0;
      if (row[4]) a["starts_at"]  = std::stoull(row[4]);
      if (row[5]) a["expires_at"] = std::stoull(row[5]);
      if (row[7]) a["created_at"] = std::stoull(row[7]);
      items.push_back(a);
    }
    mysql_free_result(res);

    return {
        {"card_type", "announcement_list"},
        {"data",      items},
        {"total",     total},
        {"page",      page},
        {"page_size", page_size},
        {"summary",   "共 " + std::to_string(total) + " 条公告，本页 " +
                      std::to_string(items.size()) + " 条"}
    };
  }

  // ── create_announcement（管理员，需确认）────────────────────────────────────
  if (tool_name == "create_announcement") {
    if (!is_admin) {
      return {{"card_type", "text"}, {"data", "权限不足：仅管理员可创建公告"}};
    }
    if (!pool_) {
      return {{"card_type", "text"}, {"data", "数据库连接不可用"}};
    }

    auto get_str_param = [&](const char* key) -> std::string {
      if (params.contains(key) && params[key].is_string()) return params[key].get<std::string>();
      return {};
    };

    const std::string title      = get_str_param("title");
    const std::string content    = get_str_param("content");
    const std::string expires_at = get_str_param("expires_at");
    const bool is_pinned         = params.contains("is_pinned") && params["is_pinned"].is_boolean()
                                     ? params["is_pinned"].get<bool>() : false;

    if (title.empty()) {
      return {{"card_type", "text"}, {"data", "创建公告失败：标题不能为空"}};
    }
    if (content.empty()) {
      return {{"card_type", "text"}, {"data", "创建公告失败：内容不能为空"}};
    }

    auto conn_guard = pool_->GetConnection();
    MYSQL* conn = conn_guard.Get();
    if (!conn) {
      return {{"card_type", "text"}, {"data", "数据库连接失败"}};
    }

    // 转义
    auto esc = [&](const std::string& s) -> std::string {
      std::string out(s.size() * 2 + 1, '\0');
      unsigned long len = mysql_real_escape_string(conn, out.data(), s.c_str(),
                                                   static_cast<unsigned long>(s.size()));
      out.resize(len);
      return out;
    };

    std::ostringstream q;
    q << "INSERT INTO announcements (title, content, is_pinned, starts_at, expires_at, created_by) VALUES ("
      << "'" << esc(title) << "', "
      << "'" << esc(content) << "', "
      << (is_pinned ? 1 : 0) << ", "
      << "NOW(), ";

    if (expires_at.empty()) {
      q << "NULL, ";
    } else {
      q << "'" << esc(expires_at) << "', ";
    }

    q << user_id << ")";

    if (mysql_query(conn, q.str().c_str()) != 0) {
      return {{"card_type", "text"}, {"data", std::string("创建公告失败：") + mysql_error(conn)}};
    }

    const std::uint64_t new_id = static_cast<std::uint64_t>(mysql_insert_id(conn));
    Logger::Info("AssistantService: create_announcement id=" + std::to_string(new_id) +
                 " user_id=" + std::to_string(user_id) +
                 " title=" + title);

    return {
        {"card_type", "announcement_created"},
        {"data",      "公告「" + title + "」已发布成功"},
        {"id",        new_id},
        {"title",     title},
        {"content",   content},
        {"is_pinned", is_pinned},
        {"success",   true}
    };
  }

  return {{"card_type", "text"}, {"data", "未知工具：" + tool_name}};
}

// ── Qwen API（支持 Function Calling）─────────────────────────────────────────
bool AssistantService::CallQwenAPIWithTools(const nlohmann::json& messages,
                                             const nlohmann::json& tools,
                                             std::string& out_text,
                                             nlohmann::json& out_tool_calls,
                                             std::string& error_message) const {
  // 使用 OpenAI 兼容接口支持 Function Calling
  // 旧版 DashScope 接口（api/v1/services/aigc/text-generation/generation）不支持 tools 参数
  const bool has_tools = tools.is_array() && !tools.empty();
  std::string endpoint;
  nlohmann::json payload;

  if (has_tools) {
    // OpenAI 兼容格式，支持 Function Calling
    endpoint = "https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions";
    payload = {
        {"model", kDefaultModel},
        {"messages", messages},
        {"temperature", 0.3},
        {"tools", tools},
        {"tool_choice", "auto"}
    };
  } else {
    // 无工具时使用旧版接口（向后兼容）
    endpoint = kQwenEndpoint;
    payload = {
        {"model", kDefaultModel},
        {"input", {{"messages", messages}}},
        {"parameters", {
            {"result_format", "message"},
            {"temperature", 0.3}
        }}
    };
  }

  const std::string body = payload.dump();
  std::string response_body;

  CURL* curl = curl_easy_init();
  if (!curl) {
    error_message = "curl_easy_init 失败";
    return false;
  }

  struct curl_slist* headers = nullptr;
  headers = curl_slist_append(headers, ("Authorization: Bearer " + api_key_).c_str());
  headers = curl_slist_append(headers, "Content-Type: application/json");

  curl_easy_setopt(curl, CURLOPT_URL, endpoint.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(timeout_seconds_));
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);

  const CURLcode res = curl_easy_perform(curl);
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);

  if (res != CURLE_OK) {
    error_message = std::string("curl 请求失败: ") + curl_easy_strerror(res);
    return false;
  }

  try {
    auto resp_json = nlohmann::json::parse(response_body);

    if (has_tools) {
      // OpenAI 兼容格式响应解析
      if (resp_json.contains("error")) {
        std::string msg = "Qwen API 错误";
        if (resp_json["error"].contains("message"))
          msg += ": " + resp_json["error"]["message"].get<std::string>();
        error_message = msg;
        return false;
      }
      // 提取 tool_calls（OpenAI 格式：choices[0].message.tool_calls）
      if (resp_json.contains("choices") && resp_json["choices"].is_array() &&
          !resp_json["choices"].empty()) {
        const auto& choice = resp_json["choices"][0];
        if (choice.contains("message")) {
          const auto& msg_j = choice["message"];
          if (msg_j.contains("tool_calls") && msg_j["tool_calls"].is_array()) {
            out_tool_calls = msg_j["tool_calls"];
            return true;
          }
          // 纯文本回复（OpenAI 格式）
          if (msg_j.contains("content") && msg_j["content"].is_string()) {
            out_text = msg_j["content"].get<std::string>();
          }
        }
      }
      return true;
    } else {
      // 旧版 DashScope 格式响应解析
      if (resp_json.contains("code") && resp_json["code"].is_string()) {
        const std::string code = resp_json["code"].get<std::string>();
        if (code != "200" && code != "Success") {
          std::string msg = "Qwen API 错误";
          if (resp_json.contains("message")) msg += ": " + resp_json["message"].dump();
          error_message = msg;
          return false;
        }
      }
      out_tool_calls = ExtractToolCalls(resp_json);
      if (!out_tool_calls.empty()) return true;
      out_text = ExtractText(resp_json);
      return true;
    }

  } catch (const std::exception& e) {
    error_message = std::string("解析 LLM 响应失败: ") + e.what();
    return false;
  }
}

// ── 原有 Qwen API（无 tools，向后兼容）───────────────────────────────────────
std::string AssistantService::CallQwenAPI(const std::string& system_prompt,
                                           const std::string& user_prompt,
                                           const std::vector<ChatMessage>& history,
                                           std::string& error_message) const {
  nlohmann::json messages = nlohmann::json::array();
  messages.push_back({{"role", "system"}, {"content", system_prompt}});

  const size_t max_history = kContextMessages;
  size_t start = history.size() > max_history ? history.size() - max_history : 0;
  for (size_t i = start; i < history.size(); ++i) {
    if (history[i].role == "user" || history[i].role == "assistant") {
      messages.push_back({{"role", history[i].role}, {"content", history[i].content}});
    }
  }
  messages.push_back({{"role", "user"}, {"content", user_prompt}});

  std::string out_text;
  nlohmann::json out_tool_calls;
  CallQwenAPIWithTools(messages, nlohmann::json::array(), out_text, out_tool_calls, error_message);
  return out_text;
}

// ── 旧版 JSON 响应解析（向后兼容）────────────────────────────────────────────
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

      auto get_str = [&](const char* key) -> std::string {
        if (action_j.contains("params") && action_j["params"].is_object()) {
          const auto& params = action_j["params"];
          if (params.contains(key) && params[key].is_string())
            return params[key].get<std::string>();
        }
        return {};
      };

      if (action_j.contains("intent") && action_j["intent"].is_string()) {
        action.intent = action_j["intent"].get<std::string>();
      }
      if (action_j.contains("confirm_text") && action_j["confirm_text"].is_string()) {
        action.confirm_text = action_j["confirm_text"].get<std::string>();
      }
      action.ppt_id    = get_str("ppt_id");
      action.ppt_title = get_str("ppt_title");
      action.topic     = get_str("topic");
      action.style     = get_str("style");
      action.page      = get_str("page");

      if (action_j.contains("params") && action_j["params"].is_object()) {
        const auto& params = action_j["params"];
        if (params.contains("page_count")) {
          if (params["page_count"].is_number_integer()) {
            action.page_count = params["page_count"].get<int>();
          } else if (params["page_count"].is_string()) {
            try { action.page_count = std::stoi(params["page_count"].get<std::string>()); } catch (...) {}
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

// ── MongoDB 工具方法 ──────────────────────────────────────────────────────────
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
                                    const std::string& content,
                                    const nlohmann::json& tool_cards) {
  if (!IsPersistenceEnabled()) return false;
  nlohmann::json doc = {
      {"message_id", GenerateUUID()},
      {"session_id", session_id},
      {"user_id",    static_cast<std::int64_t>(user_id)},
      {"role",       role},
      {"content",    content},
      {"timestamp",  NowISO()}
  };
  if (tool_cards.is_array() && !tool_cards.empty()) {
    doc["tool_cards"] = tool_cards;
  }
  return mongo_->InsertOne("chat_messages", doc);
}

std::vector<ChatMessage> AssistantService::LoadRecentMessages(
    const std::string& session_id, int n) {
  std::vector<ChatMessage> result;
  if (!IsPersistenceEnabled()) return result;

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
    if (doc.contains("tool_cards") && doc["tool_cards"].is_array()) {
      msg.tool_cards = doc["tool_cards"];
    }
    // 仅保留 user/assistant 角色（tool 角色不必要加载给 LLM 历史）
    if ((msg.role == "user" || msg.role == "assistant") &&
        !msg.content.empty()) {
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
      {{"timestamp", 1}},
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
    if (doc.contains("tool_cards") && doc["tool_cards"].is_array()) {
      msg.tool_cards = doc["tool_cards"];
    }
    if (!msg.role.empty()) result.push_back(std::move(msg));
  }
  return result;
}

// ── ChatInSession（Tool Call 架构核心）───────────────────────────────────────
bool AssistantService::ChatInSession(const std::string& session_id,
                                      std::uint64_t user_id,
                                      bool is_admin,
                                      const std::string& user_message,
                                      const std::string& context_json,
                                      AssistantResponse& out_response,
                                      std::string& error_message) {
  if (!IsEnabled()) {
    out_response.reply = "AI 助手暂未配置，请联系管理员。";
    return true;
  }

  // 1. 验证会话
  if (!ValidateSession(session_id, user_id, error_message)) return false;

  // 2. 保存用户消息
  SaveMessage(session_id, user_id, "user", user_message);

  // 3. 读取历史消息（仅 user/assistant 角色）
  auto history = LoadRecentMessages(session_id, kContextMessages);
  // 排除刚保存的这条 user 消息（LoadRecentMessages 已包含）
  std::vector<ChatMessage> history_without_last = history;
  if (!history_without_last.empty() &&
      history_without_last.back().role == "user") {
    history_without_last.pop_back();
  }

  // 4. 构建 messages 数组
  nlohmann::json messages = nlohmann::json::array();
  messages.push_back({{"role", "system"}, {"content", BuildSystemPrompt(is_admin)}});
  for (const auto& h : history_without_last) {
    messages.push_back({{"role", h.role}, {"content", h.content}});
  }
  // 将用户消息与上下文合并
  const std::string user_prompt = BuildUserPrompt(user_message, context_json);
  messages.push_back({{"role", "user"}, {"content", user_prompt}});

  // 5. 构建工具列表
  const nlohmann::json tools = BuildTools(is_admin);

  // 6. Tool Call 循环（最多 kMaxToolCallRounds 轮）
  std::string final_reply;
  int round = 0;

  while (round < kMaxToolCallRounds) {
    ++round;

    std::string out_text;
    nlohmann::json out_tool_calls;
    std::string llm_error;

    const bool ok = CallQwenAPIWithTools(messages, tools, out_text, out_tool_calls, llm_error);
    if (!ok) {
      Logger::Error("AssistantService::ChatInSession LLM error (round " +
                    std::to_string(round) + "): " + llm_error);
      out_response.reply = "抱歉，AI 服务暂时不可用，请稍后再试。";
      SaveMessage(session_id, user_id, "assistant", out_response.reply);
      TouchSession(session_id, history.empty() ? user_message : "");
      return true;
    }

    // 情形 A：LLM 返回纯文本（对话结束）
    if (out_tool_calls.empty()) {
      final_reply = out_text.empty() ? "抱歉，我暂时无法理解您的请求，请换个方式描述。" : out_text;
      break;
    }

    // 情形 B：LLM 发起工具调用
    // 将 LLM 的 assistant 消息（含 tool_calls）加入 messages
    nlohmann::json assistant_msg = {
        {"role", "assistant"},
        {"content", nullptr},
        {"tool_calls", out_tool_calls}
    };
    messages.push_back(assistant_msg);

    // 处理每个 tool_call
    for (const auto& tc : out_tool_calls) {
      std::string tool_call_id;
      std::string tool_name;
      nlohmann::json tool_params;

      try {
        tool_call_id = tc.value("id", "");
        if (tc.contains("function")) {
          tool_name = tc["function"].value("name", "");
          const std::string args_str = tc["function"].value("arguments", "{}");
          tool_params = nlohmann::json::parse(args_str);
        }
      } catch (...) {
        Logger::Warn("AssistantService: 解析 tool_call 失败");
        continue;
      }

      if (tool_name.empty()) continue;

      Logger::Info("AssistantService: 执行工具 " + tool_name +
                   " (user=" + std::to_string(user_id) + ")" +
                   " params=" + tool_params.dump());

      if (IsClientTool(tool_name)) {
        // 客户端工具：透传给前端
        ClientToolCall ctc;
        ctc.tool_name = tool_name;
        ctc.params = tool_params;
        // 普通危险工具：二次确认弹窗
        ctc.confirm_required = ToolRequiresConfirm(tool_name);
        // 最高级危险工具：需输入 "CONFIRM" 的特殊弹窗
        ctc.require_confirm_code = kHighDangerTools.count(tool_name) > 0;
        if (ctc.confirm_required || ctc.require_confirm_code) {
          ctc.confirm_text = BuildConfirmText(tool_name, tool_params);
          out_response.requires_confirm = true;
        }
        out_response.pending_client_tools.push_back(std::move(ctc));

        // 向 LLM 反馈客户端工具已派发（高危工具说明需要用户确认）
        std::string tool_feedback = "已派发给客户端执行";
        if (kHighDangerTools.count(tool_name) > 0) {
          tool_feedback = "工具调用已发送至前端，正在等待用户在弹窗中输入 CONFIRM 确认。操作尚未执行，须用户确认后才生效。";
        } else if (kDangerTools.count(tool_name) > 0) {
          tool_feedback = "工具调用已发送至前端，正在等待用户确认。操作尚未执行。";
        }
        nlohmann::json tool_result_msg = {
            {"role", "tool"},
            {"tool_call_id", tool_call_id},
            {"name", tool_name},
            {"content", tool_feedback}
        };
        messages.push_back(tool_result_msg);

      } else {
        // 服务端工具：直接执行，将结果反馈给 LLM
        const nlohmann::json result_json = ExecuteServerTool(
            tool_name, tool_params, user_id, is_admin);

        // 加入卡片摘要供前端展示
        ToolResultCard card;
        card.card_type = result_json.value("card_type", "text");
        card.data = result_json.contains("data") ? result_json["data"] : result_json;
        // 保存除 card_type/data 外的所有顶层字段（如 total/page/is_admin_view）
        card.meta = nlohmann::json::object();
        for (auto it = result_json.begin(); it != result_json.end(); ++it) {
          if (it.key() != "card_type" && it.key() != "data") {
            card.meta[it.key()] = it.value();
          }
        }
        out_response.tool_results_summary.push_back(std::move(card));

        // 向 LLM 反馈结果：列表类工具只传摘要（总数+分页），完整数据由前端卡片展示
        // 避免大列表超过截断限制导致 LLM 统计错误
        nlohmann::json llm_result = result_json;
        const std::string card_type_for_llm = result_json.value("card_type", "text");
        const bool is_list_card = (card_type_for_llm == "ppt_list" ||
                                   card_type_for_llm == "material_list" ||
                                   card_type_for_llm == "my_materials" ||
                                   card_type_for_llm == "template_list");
        // batch_delete_result：给 LLM 精简摘要（只保留统计数字，不传完整列表）
        if (card_type_for_llm == "batch_delete_result") {
          llm_result = {
              {"card_type", "batch_delete_result"},
              {"deleted_count", result_json.value("deleted_count", 0)},
              {"failed_count",  result_json.value("failed_count", 0)},
              {"success",       result_json.value("success", false)}
          };
          if (result_json.contains("error")) llm_result["error"] = result_json["error"];
        }
        if (is_list_card && result_json.contains("data") && result_json["data"].is_array()) {
          // 给 LLM 回传精简列表，根据卡片类型保留不同字段
          nlohmann::json slim_array = nlohmann::json::array();
          const bool is_template_card = (card_type_for_llm == "template_list");
          for (const auto& item : result_json["data"]) {
            nlohmann::json slim = nlohmann::json::object();
            if (is_template_card) {
              // 模板：保留 id/name/is_active/provider/tags
              for (const char* k : {"id", "name", "is_active", "provider", "tags"}) {
                if (item.contains(k)) slim[k] = item[k];
              }
            } else {
              // 素材：保留 id/filename/review_status/status/user_id
              for (const char* k : {"id", "filename", "review_status", "status", "user_id"}) {
                if (item.contains(k)) slim[k] = item[k];
              }
            }
            slim_array.push_back(std::move(slim));
          }
          if (result_json.contains("total")) {
            llm_result["total_in_db"] = result_json["total"];
          }
          if (is_template_card) {
            // 模板列表：仅给 LLM 两张快查表，不传完整 data（避免超截断限制）
            nlohmann::json inactive_list = nlohmann::json::array();
            nlohmann::json active_list = nlohmann::json::array();
            for (const auto& item : slim_array) {
              bool active = item.value("is_active", false);
              nlohmann::json entry = {{"id", item.value("id","")}, {"name", item.value("name","")}};
              if (active) active_list.push_back(entry);
              else        inactive_list.push_back(entry);
            }
            llm_result.erase("data");  // 模板不回传完整 data，防止截断
            llm_result["inactive_templates"] = inactive_list;
            llm_result["active_templates"]   = active_list;
            llm_result["note"] = "inactive_templates=未上架可执行activate的模板（含id/name），active_templates=已上架可执行deactivate的模板（含id/name）。执行上架/下架时直接用对应条目的id，禁止猜测或编造id。";
          } else {
            llm_result["data"] = std::move(slim_array);
            llm_result["note"] = "完整卡片已在前端展示，data 为精简列表（含 id/filename/review_status/status/user_id），供您引用 id 调用后续工具";
          }
        }
        const std::string result_str = TruncateToolResult(llm_result.dump());
        nlohmann::json tool_result_msg = {
            {"role", "tool"},
            {"tool_call_id", tool_call_id},
            {"name", tool_name},
            {"content", result_str}
        };
        messages.push_back(tool_result_msg);
      }
    }

    // 如果所有工具都是客户端工具，需要额外请求 LLM 生成最终回复文本
    // 若所有都是服务端工具，继续下一轮让 LLM 综合结果给出回复
  }

  if (final_reply.empty()) {
    // 超过最大轮数或未获得最终文本，强制最后请求一次仅文本回复
    std::string out_text;
    nlohmann::json out_tool_calls_ignored;
    std::string llm_error;
    CallQwenAPIWithTools(messages, nlohmann::json::array(), out_text, out_tool_calls_ignored, llm_error);
    final_reply = out_text.empty() ? "操作已完成。" : out_text;
  }

  out_response.reply = final_reply;

  // 7. 保存 AI 回复（附带工具卡片，供历史会话恢复时展示）
  nlohmann::json cards_json = nlohmann::json::array();
  // 服务端工具结果卡片
  for (const auto& card : out_response.tool_results_summary) {
    nlohmann::json card_item = {{"card_type", card.card_type}, {"data", card.data}};
    if (card.meta.is_object()) {
      for (auto it = card.meta.begin(); it != card.meta.end(); ++it) {
        card_item[it.key()] = it.value();
      }
    }
    cards_json.push_back(std::move(card_item));
  }
  // 客户端工具的摘要卡片：将 pending_client_tools 中含有 ppt_list 的工具以卡片形式持久化
  for (const auto& ct : out_response.pending_client_tools) {
    if (ct.tool_name == "batch_download_ppt" && ct.params.contains("ppt_list")) {
      cards_json.push_back({
        {"card_type", "batch_download"},
        {"data", ct.params["ppt_list"]},
        {"total", ct.params["ppt_list"].is_array() ? ct.params["ppt_list"].size() : 0}
      });
    } else if (ct.tool_name == "batch_delete_ppt" && ct.params.contains("ppt_list")) {
      // batch_delete_ppt 由前端确认后执行，结果卡片由前端追加；此处仅记录意图卡片
      cards_json.push_back({
        {"card_type", "batch_delete_intent"},
        {"data", ct.params["ppt_list"]},
        {"total", ct.params["ppt_list"].is_array() ? ct.params["ppt_list"].size() : 0}
      });
    }
  }
  SaveMessage(session_id, user_id, "assistant", final_reply, cards_json);

  // 8. 更新会话时间
  const bool is_first = (mongo_->Count("chat_messages", {{"session_id", session_id}}) <= 2);
  TouchSession(session_id, is_first ? user_message : "");

  return true;
}
