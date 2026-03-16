#include "services/ai_native_ppt_service.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <array>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include "logger.h"
#include "services/wanxiang_image_client.h"

namespace {

constexpr const char* kQwenEndpoint =
    "https://dashscope.aliyuncs.com/api/v1/services/aigc/text-generation/generation";
constexpr const char* kQwenModel = "qwen-plus";

size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
  size_t total = size * nmemb;
  static_cast<std::string*>(userp)->append(static_cast<char*>(contents), total);
  return total;
}

std::string ExtractTextFromQwenResponse(const nlohmann::json& resp) {
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

// 将大纲列表序列化为简洁 JSON 字符串
std::string OutlineToJson(const std::vector<OutlineItem>& outline) {
  nlohmann::json arr = nlohmann::json::array();
  for (size_t i = 0; i < outline.size(); ++i) {
    nlohmann::json item;
    item["index"] = static_cast<int>(i);
    item["title"] = outline[i].title;
    if (!outline[i].summary.empty()) {
      item["summary"] = outline[i].summary;
    }
    arr.push_back(item);
  }
  return arr.dump(2);
}

// 写临时文件，返回路径
std::string WriteTempFile(const std::string& content, const std::string& suffix) {
  const std::string path = std::filesystem::temp_directory_path().string()
                           + "/ai_native_" + std::to_string(std::hash<std::string>{}(content))
                           + suffix;
  std::ofstream ofs(path, std::ios::trunc);
  if (!ofs) {
    return {};
  }
  ofs << content;
  return path;
}

// 下载 URL 到本地文件
size_t AiNativeWriteFileCallback(void* contents, size_t size, size_t nmemb, void* userp) {
  auto* file = static_cast<std::ofstream*>(userp);
  const auto total = size * nmemb;
  file->write(static_cast<const char*>(contents), static_cast<std::streamsize>(total));
  return total;
}

bool AiNativeDownloadToFile(const std::string& url,
                             const std::string& path,
                             std::uint32_t timeout_seconds,
                             std::string& error) {
  CURL* curl = curl_easy_init();
  if (!curl) {
    error = "无法初始化HTTP客户端";
    return false;
  }
  std::filesystem::create_directories(std::filesystem::path(path).parent_path());
  std::ofstream output(path, std::ios::binary);
  if (!output.is_open()) {
    curl_easy_cleanup(curl);
    error = "无法写入图片文件: " + path;
    return false;
  }
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, AiNativeWriteFileCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &output);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(timeout_seconds));
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);
  curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
  CURLcode res = curl_easy_perform(curl);
  long http_code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
  curl_easy_cleanup(curl);
  output.close();
  if (res != CURLE_OK) {
    error = curl_easy_strerror(res);
    return false;
  }
  if (http_code < 200 || http_code >= 300) {
    error = "HTTP " + std::to_string(http_code);
    return false;
  }
  return true;
}

// 构建图片本地保存路径
std::string BuildAiNativeImagePath(const std::string& image_dir,
                                    std::uint64_t request_id,
                                    int slide_idx,
                                    int img_idx) {
  std::filesystem::path dir(image_dir.empty() ? std::filesystem::temp_directory_path().string() : image_dir);
  std::ostringstream name;
  name << "ai_native_" << request_id << "_s" << slide_idx << "_i" << img_idx << ".jpg";
  return (dir / name.str()).lexically_normal().string();
}

}  // namespace

AiNativePptService::AiNativePptService(std::string qwen_api_key,
                                        std::uint32_t timeout_seconds)
    : qwen_api_key_(std::move(qwen_api_key)),
      timeout_seconds_(timeout_seconds) {}

// ---------------------------------------------------------------------------
// 私有：调用 Qwen API，返回文本内容
// ---------------------------------------------------------------------------
std::string AiNativePptService::CallQwen(const std::string& system_prompt,
                                          const std::string& user_prompt,
                                          std::string& error) const {
  nlohmann::json messages = nlohmann::json::array();
  messages.push_back({{"role", "system"}, {"content", system_prompt}});
  messages.push_back({{"role", "user"}, {"content", user_prompt}});

  nlohmann::json payload = {
      {"model", kQwenModel},
      {"input", {{"messages", messages}}},
      {"parameters", {{"result_format", "message"}, {"temperature", 0.5}, {"max_tokens", 4096}}}
  };

  const std::string body = payload.dump();
  std::string response_body;

  CURL* curl = curl_easy_init();
  if (!curl) {
    error = "curl_easy_init 失败";
    return {};
  }

  struct curl_slist* headers = nullptr;
  headers = curl_slist_append(headers, ("Authorization: Bearer " + qwen_api_key_).c_str());
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
    error = std::string("curl 请求失败: ") + curl_easy_strerror(res);
    return {};
  }

  try {
    auto resp_json = nlohmann::json::parse(response_body);
    // 检查 API 错误
    if (resp_json.contains("code") && resp_json["code"].is_string()) {
      const std::string code = resp_json["code"].get<std::string>();
      if (code != "200" && code != "") {
        std::string msg = resp_json.value("message", "未知错误");
        error = "Qwen API 错误 [" + code + "]: " + msg;
        return {};
      }
    }
    return ExtractTextFromQwenResponse(resp_json);
  } catch (const std::exception& e) {
    error = std::string("解析 Qwen 响应失败: ") + e.what();
    return {};
  }
}

// ---------------------------------------------------------------------------
// 私有：从 LLM 输出中提取 JSON 字符串（处理 markdown 代码块）
// ---------------------------------------------------------------------------
std::string AiNativePptService::ExtractJson(const std::string& text) const {
  // 去掉 ```json ... ``` 包裹
  auto start = text.find("```json");
  if (start != std::string::npos) {
    start += 7;
    auto end = text.find("```", start);
    if (end != std::string::npos) {
      return text.substr(start, end - start);
    }
  }
  // 去掉普通 ``` 包裹
  start = text.find("```");
  if (start != std::string::npos) {
    start += 3;
    auto end = text.find("```", start);
    if (end != std::string::npos) {
      return text.substr(start, end - start);
    }
  }
  // 直接找 { ... }
  auto brace_start = text.find('{');
  auto brace_end   = text.rfind('}');
  if (brace_start != std::string::npos && brace_end != std::string::npos && brace_end > brace_start) {
    return text.substr(brace_start, brace_end - brace_start + 1);
  }
  return text;
}

// ---------------------------------------------------------------------------
// Phase 1：创意策划
// ---------------------------------------------------------------------------
bool AiNativePptService::GenerateCreativeBrief(const std::string& topic,
                                                const std::string& style,
                                                int pages,
                                                const std::string& ai_style_prompt,
                                                std::string& out_brief_json,
                                                std::string& error) const {
  const std::string system_prompt =
      R"(你是一位专业的演示文稿视觉设计师。请分析演讲主题，输出一份创意简报（Creative Brief）。
严格输出 JSON，不要有任何额外文字、注释或 markdown 代码块。)";

  std::ostringstream user_oss;
  user_oss << "演讲主题：" << topic << "\n";
  user_oss << "风格偏好：" << (style.empty() ? "business" : style) << "\n";
  user_oss << "幻灯片数量：" << pages << "\n";
  if (!ai_style_prompt.empty()) {
    user_oss << "用户补充描述：" << ai_style_prompt << "\n";
  }
  user_oss << R"(
请输出以下 JSON 格式：
{
  "audience": "目标受众描述",
  "tone": "整体基调（如：专业严肃/活泼创意/科技未来感）",
  "palette_rationale": "配色方案选择理由（一句话）",
  "palette": {
    "primary": "#xxxxxx",
    "secondary": "#xxxxxx",
    "accent": "#xxxxxx",
    "background": "#xxxxxx",
    "text_dark": "#xxxxxx",
    "text_light": "#xxxxxx"
  },
  "typography": {
    "title_font": "字体名称（必须是 PowerPoint 内置字体，如 Arial Black/Calibri/Georgia/Cambria）",
    "body_font": "字体名称",
    "title_size": 40,
    "body_size": 16
  },
  "motif": "rounded_cards 或 sharp_geometric 或 minimal_lines 或 organic_shapes 或 tech_grid",
  "slide_structure": [
    {"index": 0, "role": "封面", "layout_type": "hero_title"},
    {"index": 1, "role": "目录", "layout_type": "bullet_list"}
  ]
}

配色规则：
- 主色（primary）用于标题栏/深色背景，不能是纯白 #FFFFFF
- 强调色（accent）与主色形成强对比，用于高亮装饰
- background 用于内容页背景，通常为浅色
- text_dark 用于浅色背景上的文字，text_light 用于深色背景上的文字
- slide_structure 数组长度必须等于幻灯片数量（)" << pages << R"(）
- layout_type 可选值：hero_title, section_divider, two_column, content_with_image, bullet_list, big_stat, quote, timeline, grid_2x2, closing
- 第一张必须是 hero_title，最后一张必须是 closing)";

  const std::string text = CallQwen(system_prompt, user_oss.str(), error);
  if (text.empty()) {
    return false;
  }

  out_brief_json = ExtractJson(text);
  // 验证是否为合法 JSON
  try {
    auto parsed = nlohmann::json::parse(out_brief_json);
    (void)parsed;
  } catch (...) {
    error = "Phase 1 输出非法 JSON: " + out_brief_json.substr(0, 200);
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// Phase 2：设计规格生成（分批，每批最多 kBatchSize 张，避免超出 token 限制）
// ---------------------------------------------------------------------------
static constexpr int kDesignSpecBatchSize = 5;

// 生成单批幻灯片的布局规格，返回 slides JSON 数组字符串
bool AiNativePptService::GenerateDesignSpec(const std::string& brief_json,
                                             const std::string& outline_json,
                                             const std::string& topic,
                                             bool include_images,
                                             bool include_charts,
                                             std::string& out_spec_json,
                                             std::string& error,
                                             int total_slides,
                                             const ProgressCallback& on_progress) const {
  // 解析 brief，提取 design_spec 全局信息
  nlohmann::json brief;
  try {
    brief = nlohmann::json::parse(brief_json);
  } catch (const std::exception& e) {
    error = std::string("Phase 2: 解析 brief 失败: ") + e.what();
    return false;
  }

  // 解析大纲，获取幻灯片列表
  nlohmann::json outline_arr;
  try {
    outline_arr = nlohmann::json::parse(outline_json);
    if (!outline_arr.is_array()) {
      // 可能是对象形式的 slide_structure
      if (outline_arr.is_object() && outline_arr.contains("slide_structure")) {
        outline_arr = outline_arr["slide_structure"];
      }
    }
  } catch (...) {
    outline_arr = nlohmann::json::array();
  }

  const int total = static_cast<int>(outline_arr.size());
  if (total == 0) {
    error = "Phase 2: 大纲为空，无法生成设计规格";
    return false;
  }

  // 构建全局 design_spec（从 brief 提取）
  nlohmann::json design_spec;
  design_spec["palette"]    = brief.value("palette",    nlohmann::json::object());
  design_spec["typography"] = brief.value("typography", nlohmann::json::object());
  design_spec["motif"]      = brief.value("motif",      "minimal_lines");
  design_spec["slide_size"] = {{"w", 13.33}, {"h", 7.5}};

  // 构建 palette/typography 摘要，供每批 prompt 引用
  std::string palette_summary;
  try {
    const auto& pal = design_spec["palette"];
    palette_summary = "primary=" + pal.value("primary", "#1E2761")
                    + " secondary=" + pal.value("secondary", "#CADCFC")
                    + " accent=" + pal.value("accent", "#F96167")
                    + " background=" + pal.value("background", "#FFFFFF")
                    + " text_dark=" + pal.value("text_dark", "#1A1A2E")
                    + " text_light=" + pal.value("text_light", "#FFFFFF");
  } catch (...) {
    palette_summary = "primary=#1E2761 secondary=#CADCFC accent=#F96167 background=#FFFFFF text_dark=#1A1A2E text_light=#FFFFFF";
  }
  std::string typo_summary;
  try {
    const auto& typo = design_spec["typography"];
    typo_summary = "title_font=" + typo.value("title_font", "Arial Black")
                 + " body_font=" + typo.value("body_font", "Calibri")
                 + " title_size=" + std::to_string(typo.value("title_size", 40))
                 + " body_size=" + std::to_string(typo.value("body_size", 16));
  } catch (...) {
    typo_summary = "title_font=Arial Black body_font=Calibri title_size=40 body_size=16";
  }

  const std::string system_prompt =
      "你是一位精通 PowerPoint 排版的视觉设计工程师。\n"
      "根据全局设计规格和幻灯片大纲，为指定批次的幻灯片生成精确的元素布局规格。\n"
      "严格输出 JSON 数组，不要有任何额外文字、注释或 markdown 代码块。";

  // 分批生成
  nlohmann::json all_slides = nlohmann::json::array();

  for (int batch_start = 0; batch_start < total; batch_start += kDesignSpecBatchSize) {
    const int batch_end = std::min(batch_start + kDesignSpecBatchSize, total);
    Logger::Info("AiNativePptService: Phase 2 batch " + std::to_string(batch_start)
                 + "-" + std::to_string(batch_end - 1) + " / " + std::to_string(total - 1));

    // 构建本批大纲子集
    nlohmann::json batch_outline = nlohmann::json::array();
    for (int i = batch_start; i < batch_end; ++i) {
      batch_outline.push_back(outline_arr[i]);
    }

    std::ostringstream user_oss;
    user_oss << "演讲主题：" << topic << "\n\n";
    user_oss << "全局配色：" << palette_summary << "\n";
    user_oss << "全局字体：" << typo_summary << "\n";
    user_oss << "视觉母题：" << design_spec.value("motif", "minimal_lines") << "\n\n";
    user_oss << "本批幻灯片大纲（共 " << (batch_end - batch_start) << " 张）：\n"
             << batch_outline.dump(2) << "\n\n";
    user_oss << "请为以上每张幻灯片输出布局规格，格式为 JSON 数组：\n"
             << "[\n"
             << "  {\n"
             << "    \"index\": 0,\n"
             << "    \"layout_type\": \"hero_title\",\n"
             << "    \"title\": \"__TITLE__\",\n"
             << "    \"subtitle\": \"\",\n"
             << "    \"background_color\": \"#xxxxxx\",\n"
             << "    \"image_prompt\": \"English description or empty string\",\n"
             << "    \"notes\": \"\",\n"
             << "    \"elements\": [\n"
             << "      { \"type\": \"shape\", \"shape\": \"rect\", \"x\": 0, \"y\": 0, \"w\": 13.33, \"h\": 7.5, \"fill\": \"#xxxxxx\" },\n"
             << "      { \"type\": \"text\", \"content\": \"__TITLE__\", \"x\": 1, \"y\": 2, \"w\": 11, \"h\": 2, \"font_size\": 44, \"bold\": true, \"color\": \"#ffffff\", \"align\": \"left\" }\n"
             << "    ]\n"
             << "  }\n"
             << "]\n\n"
             << "布局规则：\n"
             << "1. 坐标单位为英寸，幻灯片 13.33x7.5，最小边距 0.5 英寸\n"
             << "2. 元素顺序：背景形状 -> 装饰形状 -> 图片/图表 -> 文字\n"
             << "3. 正文字号>=14，标题字号>=28\n"
             << "4. 每张幻灯片至少 1 个 shape 元素\n"
             << "5. index=0 和最后一张使用深色背景（primary 色）\n"
             << "6. 文字占位符：标题用 __TITLE__，副标题用 __SUBTITLE__，正文用 __BODY__\n"
             << "7. bullets 格式：{\"type\":\"bullets\",\"items\":[\"__BULLET_0__\",\"__BULLET_1__\"],\"x\":...,\"y\":...,\"w\":...,\"h\":...,\"font_size\":16,\"color\":\"#...\"}\n"
             << "8. 只输出 JSON 数组，不要包含 design_spec 字段\n";

    if (include_images) {
      user_oss << "9. 【图片规则】内容页（非封面/结尾）可以包含 image 元素，格式：\n"
               << "   {\"type\":\"image\",\"path\":\"__IMAGE_PLACEHOLDER__\",\"x\":...,\"y\":...,\"w\":...,\"h\":...,\"sizing\":\"cover\"}\n"
               << "   image_prompt 字段必须是英文，描述具体画面，不超过 30 词\n"
               << "   每张幻灯片最多 1 个 image 元素，封面/结尾页不需要图片\n";
    } else {
      user_oss << "9. 【图片规则】本次不需要图片，不要生成 image 类型元素，image_prompt 留空字符串\n";
    }

    if (include_charts) {
      user_oss << "10. 【图表规则】数据类/对比类幻灯片可以包含 chart 元素，格式：\n"
               << "    {\"type\":\"chart\",\"chart_type\":\"bar\",\"x\":...,\"y\":...,\"w\":...,\"h\":...,\n"
               << "     \"title\":\"图表标题\",\n"
               << "     \"categories\":[\"类别A\",\"类别B\",\"类别C\"],\n"
               << "     \"series\":[{\"name\":\"系列1\",\"values\":[30,50,20]}]}\n"
               << "    chart_type 可选：bar（柱状图）、line（折线图）、pie（饼图）\n"
               << "    每张幻灯片最多 1 个 chart 元素，values 为数字数组\n";
    } else {
      user_oss << "10. 【图表规则】本次不需要图表，不要生成 chart 类型元素\n";
    }

    std::string call_error;
    const std::string text = CallQwen(system_prompt, user_oss.str(), call_error);
    if (text.empty()) {
      error = "Phase 2 batch " + std::to_string(batch_start) + " 调用失败: " + call_error;
      return false;
    }

    // 提取 JSON 数组
    const std::string raw = ExtractJson(text);
    // ExtractJson 找 { ... }，但这里是数组，需要找 [ ... ]
    std::string arr_str = raw;
    {
      auto ab = text.find('[');
      auto ae = text.rfind(']');
      if (ab != std::string::npos && ae != std::string::npos && ae > ab) {
        arr_str = text.substr(ab, ae - ab + 1);
      }
    }

    try {
      auto batch_slides = nlohmann::json::parse(arr_str);
      if (!batch_slides.is_array()) {
        error = "Phase 2 batch " + std::to_string(batch_start) + " 输出不是数组";
        return false;
      }
      for (auto& s : batch_slides) {
        all_slides.push_back(s);
      }
    } catch (const std::exception& e) {
      error = "Phase 2 batch " + std::to_string(batch_start) + " JSON 解析失败: " + e.what()
              + " raw=" + arr_str.substr(0, 200);
      return false;
    }

    // 每个 batch 完成后上报进度（Phase 2 占进度区间 25%~52%）
    if (on_progress && total_slides > 0) {
      const int batches_total = (total_slides + kDesignSpecBatchSize - 1) / kDesignSpecBatchSize;
      const int batch_idx = batch_start / kDesignSpecBatchSize;
      const int prog = 25 + static_cast<int>((batch_idx + 1) * 27.0 / batches_total);
      on_progress(prog, "设计版式",
                  "正在设计第 " + std::to_string(batch_end) + "/" + std::to_string(total_slides) + " 张幻灯片布局...");
    }
  }

  // 组合最终 DesignSpec
  nlohmann::json result;
  result["design_spec"] = design_spec;
  result["slides"]      = all_slides;
  out_spec_json = result.dump(2);
  return true;
}

// ---------------------------------------------------------------------------
// Phase 3：填充幻灯片文字内容
// ---------------------------------------------------------------------------
bool AiNativePptService::FillSlideContents(const std::string& topic,
                                            const std::string& brief_json,
                                            std::string& inout_spec_json,
                                            std::string& error,
                                            int total_slides,
                                            const ProgressCallback& on_progress) const {
  nlohmann::json spec;
  try {
    spec = nlohmann::json::parse(inout_spec_json);
  } catch (const std::exception& e) {
    error = std::string("FillSlideContents: 解析 spec 失败: ") + e.what();
    return false;
  }

  // 从 brief 提取基调摘要
  std::string brief_summary;
  try {
    auto brief = nlohmann::json::parse(brief_json);
    brief_summary = brief.value("tone", "") + "，受众：" + brief.value("audience", "");
  } catch (...) {
    brief_summary = "专业商务";
  }

  auto& slides = spec["slides"];
  if (!slides.is_array()) {
    error = "slides 字段不是数组";
    return false;
  }

  const std::string system_prompt =
      R"(你是一位专业的演讲内容撰写师。请为指定幻灯片生成具体的文字内容。
严格输出 JSON，不要有任何额外文字或 markdown 代码块。)";

  for (auto& slide : slides) {
    const int idx = slide.value("index", 0);
    const std::string layout = slide.value("layout_type", "bullet_list");
    const std::string current_title = slide.value("title", "");

    // 跳过已有真实内容的幻灯片（非占位符）
    bool has_placeholder = (current_title.find("__") != std::string::npos
                             || current_title.find("占位") != std::string::npos
                             || current_title.empty());
    if (!has_placeholder) {
      // 仍需替换 elements 中的占位符
    }

    std::ostringstream user_oss;
    user_oss << "演讲主题：" << topic << "\n";
    user_oss << "整体基调：" << brief_summary << "\n";
    user_oss << "本张幻灯片序号：" << idx << "\n";
    user_oss << "布局类型：" << layout << "\n";
    if (!current_title.empty() && current_title.find("__") == std::string::npos) {
      user_oss << "参考标题：" << current_title << "\n";
    }
    user_oss << "\n请输出 JSON：\n"
             << "{\n"
             << "  \"title\": \"幻灯片标题（不超过18个汉字）\",\n"
             << "  \"subtitle\": \"副标题（如适用，否则空字符串）\",\n"
             << "  \"bullets\": [\"要点1（不超过28个汉字）\", \"要点2\", \"要点3\"],\n"
             << "  \"image_prompt\": \"English image description for AI generation (leave empty if no image needed)\",\n"
             << "  \"notes\": \"演讲者备注（2-3句话，指导演讲者如何讲解本页）\"\n"
             << "}\n"
             << "要求：\n"
             << "- 内容精炼专业，避免废话\n"
             << "- bullets 数量根据布局类型决定：bullet_list/two_column 为 4-6 条，其他为 2-3 条\n"
             << "- closing 类型的 title 通常是\"谢谢\"/\"感谢聆听\"等\n"
             << "- image_prompt 必须是英文，描述具体画面内容\n";

    std::string call_error;
    const std::string text = CallQwen(system_prompt, user_oss.str(), call_error);
    if (text.empty()) {
      Logger::Warn("AiNativePptService: Phase 3 slide " + std::to_string(idx)
                   + " 内容生成失败: " + call_error + "，使用占位内容");
      slide["title"] = topic + " - 第" + std::to_string(idx + 1) + "页";
      slide["subtitle"] = "";
      slide["notes"] = "";
    } else {
      const std::string content_json_str = ExtractJson(text);
      try {
        auto content = nlohmann::json::parse(content_json_str);
        const std::string title    = content.value("title", "");
        const std::string subtitle = content.value("subtitle", "");
        const std::string notes    = content.value("notes", "");
        const std::string img_prompt = content.value("image_prompt", "");

        if (!title.empty()) slide["title"] = title;
        if (!subtitle.empty()) slide["subtitle"] = subtitle;
        if (!notes.empty()) slide["notes"] = notes;
        if (!img_prompt.empty() && slide.value("image_prompt", "").empty()) {
          slide["image_prompt"] = img_prompt;
        }

        std::vector<std::string> bullets;
        if (content.contains("bullets") && content["bullets"].is_array()) {
          for (const auto& b : content["bullets"]) {
            if (b.is_string()) bullets.push_back(b.get<std::string>());
          }
        }

        if (slide.contains("elements") && slide["elements"].is_array()) {
          for (auto& el : slide["elements"]) {
            if (el["type"] == "text") {
              std::string c = el.value("content", "");
              if (c == "__TITLE__") el["content"] = title;
              else if (c == "__SUBTITLE__") el["content"] = subtitle;
              else if (c == "__BODY__") el["content"] = (!bullets.empty() ? bullets[0] : "");
            } else if (el["type"] == "bullets") {
              if (el.contains("items") && el["items"].is_array() && !bullets.empty()) {
                nlohmann::json new_items = nlohmann::json::array();
                for (size_t bi = 0; bi < el["items"].size() && bi < bullets.size(); ++bi) {
                  new_items.push_back(bullets[bi]);
                }
                for (size_t bi = el["items"].size(); bi < bullets.size(); ++bi) {
                  new_items.push_back(bullets[bi]);
                }
                el["items"] = new_items;
              }
            }
          }
        }
      } catch (const std::exception& e) {
        Logger::Warn("AiNativePptService: Phase 3 slide " + std::to_string(idx)
                     + " JSON 解析失败: " + e.what());
        slide["title"] = topic + " - 第" + std::to_string(idx + 1) + "页";
      }
    }

    // 每张幻灯片完成后上报进度（Phase 3 占进度区间 52%~72%）
    if (on_progress && total_slides > 0) {
      const int slide_done = idx + 1;
      const int prog = 52 + static_cast<int>(slide_done * 20.0 / total_slides);
      on_progress(prog, "填充内容",
                  "正在填充第 " + std::to_string(slide_done) + "/" + std::to_string(total_slides) + " 张幻灯片内容...");
    }
  }

  inout_spec_json = spec.dump(2);
  return true;
}

// ---------------------------------------------------------------------------
// Phase 4：为 image 元素调用 Wanxiang 生成真实图片，写回 elements[].path
// ---------------------------------------------------------------------------
bool AiNativePptService::FetchImages(std::string& inout_spec_json,
                                      const AiNativeGenerationConfig& config,
                                      std::uint64_t request_id,
                                      WanxiangImageClient* wanx_client,
                                      std::string& error,
                                      int total_images,
                                      const ProgressCallback& on_progress) const {
  if (!wanx_client || !wanx_client->IsEnabled()) {
    Logger::Warn("AiNativePptService: Phase 4 跳过（Wanxiang 未启用）");
    return true;
  }

  nlohmann::json spec;
  try {
    spec = nlohmann::json::parse(inout_spec_json);
  } catch (const std::exception& e) {
    error = std::string("FetchImages: 解析 spec 失败: ") + e.what();
    return false;
  }

  auto& slides = spec["slides"];
  if (!slides.is_array()) {
    return true;
  }

  const std::uint32_t timeout = wanx_client->timeout_seconds() > 0
                                    ? wanx_client->timeout_seconds()
                                    : 60;
  int img_global_idx = 0;

  for (auto& slide : slides) {
    const int slide_idx = slide.value("index", 0);
    // 取本张幻灯片的 image_prompt（Phase 3 已填充）
    const std::string slide_img_prompt = slide.value("image_prompt", "");

    if (!slide.contains("elements") || !slide["elements"].is_array()) {
      continue;
    }

    for (auto& el : slide["elements"]) {
      if (el.value("type", "") != "image") {
        continue;
      }

      // 已有真实路径则跳过
      const std::string existing_path = el.value("path", "");
      if (!existing_path.empty() && existing_path.find("__IMAGE_") != 0) {
        continue;
      }

      // 确定 prompt：优先用元素自身的 prompt，其次用幻灯片级别的 image_prompt
      std::string prompt = el.value("image_prompt", "");
      if (prompt.empty()) prompt = slide_img_prompt;
      if (prompt.empty()) {
        prompt = slide.value("title", "professional presentation illustration");
      }
      // 截断过长 prompt
      if (prompt.size() > 200) prompt.resize(200);

      Logger::Info("AiNativePptService: Phase 4 生成图片 slide=" + std::to_string(slide_idx)
                   + " prompt=" + prompt.substr(0, 60));

      std::vector<std::string> urls;
      std::string gen_error;
      if (!wanx_client->GenerateImages(prompt, urls, gen_error)) {
        // 内容审核失败时用通用安全提示词重试
        if (gen_error.find("DataInspectionFailed") != std::string::npos) {
          std::string retry_error;
          if (!wanx_client->GenerateImages("professional business presentation illustration flat design", urls, retry_error)) {
            Logger::Warn("AiNativePptService: Phase 4 图片生成失败 slide=" + std::to_string(slide_idx)
                         + " error=" + gen_error);
            ++img_global_idx;
            continue;
          }
        } else {
          Logger::Warn("AiNativePptService: Phase 4 图片生成失败 slide=" + std::to_string(slide_idx)
                       + " error=" + gen_error);
          ++img_global_idx;
          continue;
        }
      }

      if (urls.empty()) {
        ++img_global_idx;
        continue;
      }

      // 下载第一张图片到本地
      const std::string img_path = BuildAiNativeImagePath(config.image_dir, request_id, slide_idx, img_global_idx);
      std::string dl_error;
      if (AiNativeDownloadToFile(urls[0], img_path, timeout, dl_error)) {
        el["path"] = img_path;
        Logger::Info("AiNativePptService: Phase 4 图片已保存 " + img_path);
      } else {
        Logger::Warn("AiNativePptService: Phase 4 图片下载失败: " + dl_error + " url=" + urls[0]);
      }

      // 每张图片完成后上报进度（Phase 4 占进度区间 72%~88%）
      ++img_global_idx;
      if (on_progress && total_images > 0) {
        const int prog = 72 + static_cast<int>(img_global_idx * 16.0 / total_images);
        on_progress(prog, "配图生成",
                    "正在生成第 " + std::to_string(img_global_idx) + "/" + std::to_string(total_images) + " 张配图...");
      }
    }
  }

  inout_spec_json = spec.dump(2);
  return true;
}

// ---------------------------------------------------------------------------
// Phase 5：调用 ai_native_builder.js 渲染 PPTX
// ---------------------------------------------------------------------------
bool AiNativePptService::RunAiNativeBuilder(const std::string& spec_json,
                                             const std::string& output_path,
                                             const AiNativeGenerationConfig& config,
                                             std::string& error) const {
  // 写临时 JSON 文件
  const std::string tmp_json = WriteTempFile(spec_json, ".json");
  if (tmp_json.empty()) {
    error = "无法写入临时 DesignSpec JSON 文件";
    return false;
  }

  // 确保输出目录存在
  std::filesystem::path out_p(output_path);
  std::error_code ec;
  std::filesystem::create_directories(out_p.parent_path(), ec);

  // 构造命令
  const std::string cmd = "\"" + config.node_binary + "\" "
                          + "\"" + config.ai_native_builder_script + "\" "
                          + "--data-json \"" + tmp_json + "\" "
                          + "--output \"" + output_path + "\" 2>&1";

  Logger::Info("AiNativePptService: 执行渲染命令: " + cmd);

  FILE* pipe = popen(cmd.c_str(), "r");
  if (!pipe) {
    error = "popen 失败，无法执行 ai_native_builder.js";
    std::filesystem::remove(tmp_json, ec);
    return false;
  }

  std::string output_log;
  std::array<char, 256> buf{};
  while (fgets(buf.data(), buf.size(), pipe) != nullptr) {
    output_log += buf.data();
  }
  const int exit_code = pclose(pipe);
  std::filesystem::remove(tmp_json, ec);

  if (exit_code != 0) {
    error = "ai_native_builder.js 执行失败 (exit=" + std::to_string(exit_code)
            + "): " + output_log;
    return false;
  }

  if (!std::filesystem::exists(output_path)) {
    error = "ai_native_builder.js 未生成输出文件: " + output_path;
    return false;
  }

  Logger::Info("AiNativePptService: 渲染完成: " + output_path);
  return true;
}

// ---------------------------------------------------------------------------
// 公开：完整生成流程
// ---------------------------------------------------------------------------
bool AiNativePptService::Generate(const std::string& topic,
                                   const std::string& style,
                                   int pages,
                                   const std::string& ai_style_prompt,
                                   const std::vector<OutlineItem>& outline,
                                   const std::string& output_path,
                                   const AiNativeGenerationConfig& config,
                                   std::string& error_message,
                                   bool include_images,
                                   bool include_charts,
                                   WanxiangImageClient* wanx_client,
                                   ProgressCallback on_progress,
                                   const std::string& material_context) const {
  if (!IsEnabled()) {
    error_message = "AiNativePptService: Qwen API key 未配置";
    return false;
  }

  const std::string effective_topic = material_context.empty()
                                          ? topic
                                          : material_context + "\n主题：「" + topic + "」";

  Logger::Info("AiNativePptService: 开始生成，主题=" + topic
               + "，页数=" + std::to_string(pages)
               + "，include_images=" + (include_images ? "true" : "false")
               + "，include_charts=" + (include_charts ? "true" : "false")
               + (material_context.empty() ? "" : "，使用文献约束"));

  // Phase 1: 创意策划（进度 10%~25%）
  if (on_progress) on_progress(10, "AI 创意分析", "正在分析主题，生成设计创意方案...");
  std::string brief_json;
  std::string phase1_error;
  if (!GenerateCreativeBrief(effective_topic, style, pages, ai_style_prompt, brief_json, phase1_error)) {
    error_message = "Phase 1 失败: " + phase1_error;
    return false;
  }
  Logger::Info("AiNativePptService: Phase 1 完成，brief 长度=" + std::to_string(brief_json.size()));
  if (on_progress) on_progress(25, "设计版式", "创意方案已生成，开始设计每张幻灯片版式...");

  // 构建大纲 JSON
  std::string outline_json;
  if (!outline.empty()) {
    outline_json = OutlineToJson(outline);
  } else {
    try {
      auto brief = nlohmann::json::parse(brief_json);
      if (brief.contains("slide_structure") && brief["slide_structure"].is_array()) {
        outline_json = brief["slide_structure"].dump(2);
      }
    } catch (...) {}
    if (outline_json.empty()) {
      nlohmann::json default_outline = nlohmann::json::array();
      default_outline.push_back({{"index", 0}, {"role", "封面"}, {"layout_type", "hero_title"}});
      for (int i = 1; i < pages - 1; ++i) {
        default_outline.push_back({{"index", i}, {"role", "内容页" + std::to_string(i)}, {"layout_type", "bullet_list"}});
      }
      default_outline.push_back({{"index", pages - 1}, {"role", "结尾"}, {"layout_type", "closing"}});
      outline_json = default_outline.dump(2);
    }
  }

  // 计算实际幻灯片总数（用于进度计算）
  int total_slides = pages;
  try {
    auto oa = nlohmann::json::parse(outline_json);
    if (oa.is_array()) total_slides = static_cast<int>(oa.size());
  } catch (...) {}

  // Phase 2: 设计规格生成，每个 batch 完成时通过回调上报（25%~52%）
  std::string spec_json;
  std::string phase2_error;
  if (!GenerateDesignSpec(brief_json, outline_json, effective_topic, include_images, include_charts,
                          spec_json, phase2_error, total_slides, on_progress)) {
    error_message = "Phase 2 失败: " + phase2_error;
    return false;
  }
  Logger::Info("AiNativePptService: Phase 2 完成，spec 长度=" + std::to_string(spec_json.size()));
  if (on_progress) on_progress(52, "填充内容", "版式设计完成，开始为每张幻灯片填充内容...");

  // Phase 3: 填充幻灯片内容，每张完成时上报（52%~72%）
  std::string phase3_error;
  if (!FillSlideContents(effective_topic, brief_json, spec_json, phase3_error, total_slides, on_progress)) {
    Logger::Warn("AiNativePptService: Phase 3 部分失败: " + phase3_error);
  }
  Logger::Info("AiNativePptService: Phase 3 完成");

  // Phase 4: 图片生成，每张完成时上报（72%~88%）
  if (include_images && wanx_client && wanx_client->IsEnabled()) {
    // 预先统计需要生成图片的数量
    int image_count = 0;
    try {
      auto spec = nlohmann::json::parse(spec_json);
      if (spec.contains("slides") && spec["slides"].is_array()) {
        for (const auto& s : spec["slides"]) {
          if (!s.contains("elements")) continue;
          for (const auto& el : s["elements"]) {
            if (el.value("type", "") == "image") ++image_count;
          }
        }
      }
    } catch (...) {}
    if (image_count == 0) image_count = 1;  // 防止除零

    if (on_progress) on_progress(72, "配图生成", "正在为幻灯片生成 AI 配图...");
    const std::uint64_t req_id = std::hash<std::string>{}(output_path) & 0xFFFFFFFF;
    std::string phase4_error;
    if (!FetchImages(spec_json, config, req_id, wanx_client, phase4_error, image_count, on_progress)) {
      Logger::Warn("AiNativePptService: Phase 4 图片获取部分失败: " + phase4_error);
    }
    Logger::Info("AiNativePptService: Phase 4 完成");
  }

  // Phase 5: 渲染
  if (on_progress) on_progress(88, "渲染文件", "正在将内容渲染为 PPT 文件...");
  std::string render_error;
  if (!RunAiNativeBuilder(spec_json, output_path, config, render_error)) {
    error_message = "Phase 5 渲染失败: " + render_error;
    return false;
  }

  return true;
}
