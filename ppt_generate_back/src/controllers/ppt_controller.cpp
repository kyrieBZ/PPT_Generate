#include "controllers/ppt_controller.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fcntl.h>
#include <unistd.h>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <sstream>
#include <unordered_map>
#include <vector>

#include <curl/curl.h>

#include "http/http_types.h"
#include "logger.h"
#include "models/outline_item.h"
#include "models/slide_content.h"
#include "services/ai_native_ppt_service.h"
#include "services/material_service.h"
#include "utils/ppt_metrics.h"

#include <chrono>

namespace {
constexpr int kTemplateAnalysisVersion = 2;
constexpr int kLayoutGuideCacheVersion = 2;

std::string FormatTimestamp(std::uint64_t seconds) {
  if (seconds == 0) {
    return {};
  }
  std::time_t tt = static_cast<std::time_t>(seconds);
#if defined(_WIN32)
  std::tm tm_snapshot;
  gmtime_s(&tm_snapshot, &tt);
#else
  std::tm tm_snapshot;
  gmtime_r(&tt, &tm_snapshot);
#endif

  char buffer[32];
  if (std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &tm_snapshot) == 0) {
    return {};
  }
  return buffer;
}

/** UTF-8 replacement character. */
static const unsigned char kUtf8Replacement[] = { 0xEF, 0xBF, 0xBD };

/** Make string valid UTF-8 so nlohmann::json::dump() does not throw. Replaces invalid sequences with U+FFFD. */
std::string ToSafeJsonString(std::string value) {
  std::string out;
  out.reserve(value.size());
  const unsigned char* p = reinterpret_cast<const unsigned char*>(value.data());
  const unsigned char* end = p + value.size();
  while (p < end) {
    unsigned char b = *p++;
    if (b <= 0x7F) {
      if (b == 0) continue;
      if (b < 0x20 && b != '\t' && b != '\n' && b != '\r') {
        out.append(kUtf8Replacement, kUtf8Replacement + 3);
        continue;
      }
      if (b == 0x7F) {
        out.append(kUtf8Replacement, kUtf8Replacement + 3);
        continue;
      }
      out.push_back(static_cast<char>(b));
      continue;
    }
    if (b >= 0xC2 && b <= 0xDF && p + 1 <= end) {
      unsigned char b1 = p[0];
      if (b1 >= 0x80 && b1 <= 0xBF) {
        out.push_back(static_cast<char>(b));
        out.push_back(static_cast<char>(b1));
        p += 1;
        continue;
      }
    }
    if (b >= 0xE0 && b <= 0xEF && p + 2 <= end) {
      unsigned char b1 = p[0], b2 = p[1];
      if (b1 >= 0x80 && b1 <= 0xBF && b2 >= 0x80 && b2 <= 0xBF) {
        if (b != 0xE0 || b1 >= 0xA0) {
          out.push_back(static_cast<char>(b));
          out.push_back(static_cast<char>(b1));
          out.push_back(static_cast<char>(b2));
          p += 2;
          continue;
        }
      }
    }
    if (b >= 0xF0 && b <= 0xF4 && p + 3 <= end) {
      unsigned char b1 = p[0], b2 = p[1], b3 = p[2];
      if (b1 >= 0x80 && b1 <= 0xBF && b2 >= 0x80 && b2 <= 0xBF && b3 >= 0x80 && b3 <= 0xBF) {
        if (b == 0xF0 && b1 < 0x90) { /* overlong */ } else if (b == 0xF4 && b1 > 0x8F) { /* > U+10FFFF */ } else {
          out.push_back(static_cast<char>(b));
          out.push_back(static_cast<char>(b1));
          out.push_back(static_cast<char>(b2));
          out.push_back(static_cast<char>(b3));
          p += 3;
          continue;
        }
      }
    }
    out.append(kUtf8Replacement, kUtf8Replacement + 3);
  }
  return out;
}

std::string SanitizeFilenamePart(const std::string& value, std::size_t max_len) {
  std::string result;
  result.reserve(value.size());
  for (unsigned char ch : value) {
    if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '@') {
      result.push_back(static_cast<char>(ch));
    } else if (std::isspace(ch)) {
      result.push_back('_');
    } else {
      result.push_back('_');
    }
  }
  // Trim leading/trailing underscores/dots to keep filenames tidy.
  while (!result.empty() && (result.front() == '_' || result.front() == '.')) {
    result.erase(result.begin());
  }
  while (!result.empty() && (result.back() == '_' || result.back() == '.')) {
    result.pop_back();
  }
  if (result.empty()) {
    result = "ppt";
  }
  if (result.size() > max_len) {
    result.resize(max_len);
  }
  return result;
}

std::uint64_t FileMtimeSeconds(const std::filesystem::path& path) {
  std::error_code ec;
  const auto ftime = std::filesystem::last_write_time(path, ec);
  if (ec) {
    return 0;
  }
  const auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
      ftime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::seconds>(sctp.time_since_epoch()).count());
}

bool ReadJsonFile(const std::filesystem::path& path, nlohmann::json& out) {
  std::ifstream input(path);
  if (!input.is_open()) {
    return false;
  }
  try {
    input >> out;
    return true;
  } catch (...) {
    return false;
  }
}

bool WriteJsonFile(const std::filesystem::path& path, const nlohmann::json& data) {
  std::error_code ec;
  std::filesystem::create_directories(path.parent_path(), ec);
  std::ofstream output(path);
  if (!output.is_open()) {
    return false;
  }
  output << data.dump();
  return true;
}

std::filesystem::path BuildTemplateAnalysisDir(const GenerationConfig& config) {
  if (!config.template_analysis_dir.empty()) {
    return std::filesystem::path(config.template_analysis_dir);
  }
  return std::filesystem::path("assets/template_analysis");
}

std::filesystem::path BuildTemplateAnalysisPath(const GenerationConfig& config,
                                                const std::string& template_id) {
  const auto safe_id = SanitizeFilenamePart(template_id, 80);
  return BuildTemplateAnalysisDir(config) / (safe_id + ".analysis.json");
}

std::filesystem::path BuildTemplateLayoutPath(const GenerationConfig& config,
                                              const std::string& template_id,
                                              int slide_count) {
  const auto safe_id = SanitizeFilenamePart(template_id, 80);
  return BuildTemplateAnalysisDir(config) /
         (safe_id + ".layout_" + std::to_string(slide_count) + ".json");
}

bool AnalysisMatchesTemplate(const nlohmann::json& analysis,
                             const std::filesystem::path& template_path) {
  if (!analysis.is_object()) {
    return false;
  }
  const int version = analysis.value("version", 0);
  if (version != kTemplateAnalysisVersion) {
    return false;
  }
  if (!analysis.contains("template") || !analysis["template"].is_object()) {
    return false;
  }
  const auto& info = analysis["template"];
  const auto mtime = info.value("mtime", 0ULL);
  const auto size = info.value("size", 0ULL);
  std::error_code ec;
  const auto current_size = std::filesystem::file_size(template_path, ec);
  const auto current_mtime = FileMtimeSeconds(template_path);
  if (ec) {
    return false;
  }
  return static_cast<std::uint64_t>(size) == static_cast<std::uint64_t>(current_size) &&
         static_cast<std::uint64_t>(mtime) == static_cast<std::uint64_t>(current_mtime);
}

bool EnsureTemplateAnalysis(const GenerationConfig& config,
                            const std::string& template_id,
                            const std::string& template_path,
                            nlohmann::json& out_analysis,
                            std::string& error) {
  const auto analysis_path = BuildTemplateAnalysisPath(config, template_id);
  if (ReadJsonFile(analysis_path, out_analysis) &&
      AnalysisMatchesTemplate(out_analysis, template_path)) {
    return true;
  }

  if (config.python_binary.empty() || config.template_analyzer_script.empty()) {
    error = "模板分析脚本未配置";
    return false;
  }
  if (!std::filesystem::exists(config.template_analyzer_script)) {
    error = "模板分析脚本不存在";
    return false;
  }
  if (!std::filesystem::exists(template_path)) {
    error = "模板文件不存在";
    return false;
  }

  std::ostringstream command;
  command << '"' << config.python_binary << '"'
          << " \"" << config.template_analyzer_script << "\""
          << " --template \"" << template_path << "\""
          << " --output \"" << analysis_path.string() << "\"";

  const int result = std::system(command.str().c_str());
  if (result != 0) {
    error = "模板分析脚本执行失败";
    return false;
  }
  if (!ReadJsonFile(analysis_path, out_analysis)) {
    error = "无法读取模板分析结果";
    return false;
  }
  if (!AnalysisMatchesTemplate(out_analysis, template_path)) {
    error = "模板分析结果与模板文件不匹配";
    return false;
  }
  return true;
}

bool LoadLayoutGuide(const GenerationConfig& config,
                     const std::string& template_id,
                     const std::string& template_path,
                     int slide_count,
                     const std::string& template_hint,
                     const nlohmann::json& analysis,
                     const QwenClient& qwen_client,
                     std::string& out_layout_json,
                     std::string& error) {
  const auto layout_path = BuildTemplateLayoutPath(config, template_id, slide_count);
  nlohmann::json cached;
  if (ReadJsonFile(layout_path, cached) && cached.is_object()) {
    const int version = cached.value("version", 0);
    const int cached_count = cached.value("slide_count", 0);
    const bool count_ok = cached_count == slide_count;
    bool template_ok = false;
    if (cached.contains("template") && cached["template"].is_object()) {
      const auto& info = cached["template"];
      const auto mtime = info.value("mtime", 0ULL);
      const auto size = info.value("size", 0ULL);
      std::error_code ec;
      const auto current_size = std::filesystem::file_size(template_path, ec);
      const auto current_mtime = FileMtimeSeconds(template_path);
      if (!ec && static_cast<std::uint64_t>(mtime) == static_cast<std::uint64_t>(current_mtime) &&
          static_cast<std::uint64_t>(size) == static_cast<std::uint64_t>(current_size)) {
        template_ok = true;
      }
    }
    if (version == kLayoutGuideCacheVersion && count_ok && template_ok &&
        cached.contains("layout_guide")) {
      const auto& guide = cached["layout_guide"];
      if (guide.is_array()) {
        out_layout_json = guide.dump();
        return true;
      }
    }
  }

  nlohmann::json summary = analysis.value("summary", nlohmann::json::object());
  if (!summary.is_object()) {
    summary = analysis;
  }
  std::string layout_json;
  if (!qwen_client.GenerateLayoutGuide(summary.dump(), slide_count, template_hint,
                                       layout_json, error)) {
    return false;
  }
  nlohmann::json guide_json;
  try {
    guide_json = nlohmann::json::parse(layout_json);
  } catch (...) {
    error = "版式约束解析失败";
    return false;
  }
  if (!guide_json.is_array()) {
    error = "版式约束格式不正确";
    return false;
  }
  const auto template_mtime = FileMtimeSeconds(template_path);
  std::error_code ec;
  const auto template_size = std::filesystem::file_size(template_path, ec);
  if (ec) {
    return false;
  }
  nlohmann::json payload = {
      {"version", kLayoutGuideCacheVersion},
      {"template", {{"id", template_id}, {"mtime", template_mtime}, {"size", template_size}}},
      {"slide_count", slide_count},
      {"layout_guide", guide_json},
  };
  WriteJsonFile(layout_path, payload);
  out_layout_json = guide_json.dump();
  return true;
}

nlohmann::json RequestToJson(const PptRequest& request, const std::string& download_url = {},
                              const std::string& download_url_pdf = {}) {
  const bool has_file = !request.output_path.empty();
  nlohmann::json result = {
      {"id", request.id},
      {"userId", request.user_id},
      {"title", ToSafeJsonString(request.title)},
      {"topic", ToSafeJsonString(request.topic)},
      {"pages", request.pages},
      {"style", ToSafeJsonString(request.style)},
      {"includeImages", request.include_images},
      {"includeCharts", request.include_charts},
      {"includeNotes", request.include_notes},
      {"modelId", ToSafeJsonString(request.model_id)},
      {"modelName", ToSafeJsonString(request.model_name)},
      {"templateId", ToSafeJsonString(request.template_id)},
      {"templateName", ToSafeJsonString(request.template_name)},
      {"status", ToSafeJsonString(request.status)},
      {"createdAt", FormatTimestamp(request.created_at)},
      {"updatedAt", FormatTimestamp(request.updated_at)},
      {"hasFile", has_file}};
  if (!request.user_name.empty()) {
    result["username"] = ToSafeJsonString(request.user_name);
  }
  if (!request.user_email.empty()) {
    result["email"] = ToSafeJsonString(request.user_email);
  }
  if (has_file) {
    result["downloadUrl"] = download_url.empty()
                                ? "/api/ppt/file?id=" + std::to_string(request.id)
                                : ToSafeJsonString(download_url);
    if (!download_url_pdf.empty()) {
      result["downloadUrlPdf"] = ToSafeJsonString(download_url_pdf);
    }
  }
  return result;
}

nlohmann::json OutlineItemToJson(const OutlineItem& item) {
  nlohmann::json result = {
      {"title", item.title},
      {"summary", item.summary},
      {"keyPoints", item.key_points},
      {"pageType", item.page_type.empty() ? "content" : item.page_type}
  };
  return result;
}

nlohmann::json OutlineToJson(const std::vector<OutlineItem>& outline) {
  auto result = nlohmann::json::array();
  for (const auto& item : outline) {
    result.push_back(OutlineItemToJson(item));
  }
  return result;
}

std::vector<SlideContent> BuildSlidesFromOutline(const std::vector<OutlineItem>& outline,
                                                 const std::string& topic,
                                                 bool include_images) {
  std::vector<SlideContent> slides;
  for (const auto& item : outline) {
    SlideContent slide;
    slide.title = item.title;
    if (!item.key_points.empty()) {
      slide.bullets = item.key_points;
    } else if (!item.summary.empty()) {
      slide.bullets.push_back(item.summary);
    }
    slide.raw_text = slide.title;
    for (const auto& bullet : slide.bullets) {
      slide.raw_text += "\n" + bullet;
    }
    if (include_images) {
      slide.image_prompts.push_back(slide.title.empty() ? topic + " 场景" : slide.title + " 配图");
    }
    slides.push_back(std::move(slide));
  }
  return slides;
}

void AppendOutlineToPreviewJson(const std::string& output_path,
                                const std::vector<OutlineItem>& outline) {
  if (output_path.empty() || outline.empty()) {
    return;
  }
  std::filesystem::path preview_path(output_path);
  preview_path.replace_extension(".json");
  std::ifstream input(preview_path);
  if (!input.is_open()) {
    return;
  }
  nlohmann::json payload;
  try {
    input >> payload;
  } catch (...) {
    return;
  }
  payload["outline"] = OutlineToJson(outline);
  std::ofstream output(preview_path);
  if (!output.is_open()) {
    return;
  }
  output << payload.dump();
}

nlohmann::json SlideToJson(const SlideContent& slide,
                           const TemplateLayout* layout = nullptr,
                           const TemplateTheme* theme = nullptr) {
  nlohmann::json result = {{"title", slide.title}, {"rawText", slide.raw_text}};

  if (!slide.bullets.empty()) {
    result["bullets"] = slide.bullets;
  }
  if (!slide.bullet_groups.empty()) {
    result["bulletGroups"] = slide.bullet_groups;
  }

  if (layout) {
    result["layout"] = {
        {"id", layout->id},
        {"name", layout->name},
        {"type", layout->type},
        {"description", layout->description},
        {"accentColor", layout->accent_color},
        {"backgroundImage", layout->background_image}};
  } else if (!slide.layout_hint.empty()) {
    result["layoutHint"] = slide.layout_hint;
  }

  if (theme) {
    result["theme"] = {
        {"primaryColor", theme->primary_color},
        {"secondaryColor", theme->secondary_color},
        {"accentColor", theme->accent_color},
        {"backgroundImage", theme->background_image}};
  }

  if (!slide.image_prompts.empty()) {
    result["imagePrompts"] = slide.image_prompts;
  }
  if (!slide.image_urls.empty()) {
    result["imageUrls"] = slide.image_urls;
  }
  if (!slide.suggestions.empty()) {
    result["suggestions"] = slide.suggestions;
  }
  if (!slide.notes.empty()) {
    result["notes"] = slide.notes;
  }
  return result;
}

std::string ExtractToken(const HttpRequest& request) {
  auto header = request.Header("authorization");
  if (header.rfind("Bearer ", 0) == 0 || header.rfind("bearer ", 0) == 0) {
    return header.substr(7);
  }
  if (!header.empty()) {
    return header;
  }
  if (auto it = request.query_params.find("token"); it != request.query_params.end()) {
    return it->second;
  }
  return {};
}

// 构建进度文件路径（基于 request_id，生成完成前 output_path 为空时也可用）
std::string BuildProgressPath(const GenerationConfig& config, std::uint64_t request_id) {
  std::filesystem::path output_dir(config.output_dir);
  std::error_code ec;
  std::filesystem::create_directories(output_dir, ec);
  return (output_dir / ("progress_" + std::to_string(request_id) + ".json"))
      .lexically_normal()
      .string();
}

// 写入生成进度到文件（progress: 0-100, stage: 阶段描述, step: 当前步骤）
void WriteProgress(const std::string& progress_path, int progress, const std::string& stage,
                   const std::string& step = "") {
  if (progress_path.empty()) return;
  try {
    nlohmann::json j;
    j["progress"] = progress;
    j["stage"] = stage;
    if (!step.empty()) j["step"] = step;
    std::ofstream f(progress_path, std::ios::trunc);
    if (f) f << j.dump();
  } catch (...) {}
}

// 构建错误原因文件路径
std::string BuildErrorPath(const GenerationConfig& config, std::uint64_t request_id) {
  std::filesystem::path output_dir(config.output_dir);
  std::error_code ec;
  std::filesystem::create_directories(output_dir, ec);
  return (output_dir / ("error_" + std::to_string(request_id) + ".txt"))
      .lexically_normal()
      .string();
}

// 写入失败原因到文件，供前端轮询读取
void WriteErrorReason(const std::string& error_path, const std::string& reason) {
  if (error_path.empty() || reason.empty()) return;
  try {
    std::ofstream f(error_path, std::ios::trunc);
    if (f) f << reason;
  } catch (...) {}
}

std::string BuildOutputPath(const GenerationConfig& config,
                            std::uint64_t request_id,
                            const std::string& title,
                            const std::string& email) {
  std::filesystem::path output_dir(config.output_dir);
  std::error_code ec;
  std::filesystem::create_directories(output_dir, ec);
  const auto safe_title = SanitizeFilenamePart(title, 80);
  const auto safe_email = SanitizeFilenamePart(email, 80);
  std::string filename = safe_title;
  if (!safe_email.empty()) {
    filename += "_" + safe_email;
  }
  filename += "_" + std::to_string(request_id) + ".pptx";
  std::filesystem::path filepath = filename;
  return (output_dir / filepath).lexically_normal().string();
}

std::string BuildImageDir(const GenerationConfig& config, std::uint64_t request_id) {
  std::filesystem::path image_dir(config.image_dir);
  std::error_code ec;
  std::filesystem::create_directories(image_dir, ec);
  image_dir /= std::to_string(request_id);
  std::filesystem::create_directories(image_dir, ec);
  return image_dir.lexically_normal().string();
}

std::string BuildImagePath(const GenerationConfig& config,
                           std::uint64_t request_id,
                           std::size_t slide_index,
                           std::size_t image_index,
                           const std::string& ext = ".png") {
  std::filesystem::path dir(BuildImageDir(config, request_id));
  std::ostringstream name;
  name << "slide_" << (slide_index + 1) << "_img_" << (image_index + 1) << ext;
  return (dir / name.str()).lexically_normal().string();
}

size_t WriteFileCallback(void* contents, size_t size, size_t nmemb, void* userp) {
  auto* file = static_cast<std::ofstream*>(userp);
  const auto total = size * nmemb;
  file->write(static_cast<const char*>(contents), static_cast<std::streamsize>(total));
  return total;
}

bool DownloadToFile(const std::string& url,
                    const std::string& path,
                    std::uint32_t timeout_seconds,
                    std::string& error) {
  CURL* curl = curl_easy_init();
  if (!curl) {
    error = "无法初始化HTTP客户端";
    return false;
  }
  std::ofstream output(path, std::ios::binary);
  if (!output.is_open()) {
    curl_easy_cleanup(curl);
    error = "无法写入图片文件";
    return false;
  }

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteFileCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &output);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(timeout_seconds));
  curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 5L);

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
    error = "下载图片失败，HTTP " + std::to_string(http_code);
    return false;
  }
  return true;
}

std::vector<unsigned char> Base64Decode(const std::string& input) {
  static const std::string kBase64Chars =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::vector<unsigned char> output;
  int val = 0;
  int valb = -8;
  for (unsigned char c : input) {
    if (std::isspace(c)) {
      continue;
    }
    if (c == '=') {
      break;
    }
    const auto pos = kBase64Chars.find(c);
    if (pos == std::string::npos) {
      continue;
    }
    val = (val << 6) + static_cast<int>(pos);
    valb += 6;
    if (valb >= 0) {
      output.push_back(static_cast<unsigned char>((val >> valb) & 0xFF));
      valb -= 8;
    }
  }
  return output;
}

bool WriteBinaryFile(const std::string& path, const std::vector<unsigned char>& data) {
  std::ofstream output(path, std::ios::binary);
  if (!output.is_open()) {
    return false;
  }
  output.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
  return output.good();
}

std::string UrlEncodeSimple(const std::string& value) {
  static const char* hex = "0123456789ABCDEF";
  std::string encoded;
  encoded.reserve(value.size() * 3);
  for (unsigned char c : value) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      encoded.push_back(static_cast<char>(c));
    } else if (c == ' ') {
      encoded.push_back('+');
    } else {
      encoded.push_back('%');
      encoded.push_back(hex[c >> 4]);
      encoded.push_back(hex[c & 0x0F]);
    }
  }
  return encoded;
}

std::string BuildSafeImagePrompt(const SlideContent& slide, const std::string& topic) {
  std::string base = slide.title;
  if (base.empty()) {
    base = topic;
  }
  if (base.empty()) {
    return {};
  }
  const auto http_pos = base.find("http");
  if (http_pos != std::string::npos) {
    base = base.substr(0, http_pos);
  }
  for (char& c : base) {
    if (c == '"' || c == '\'' || c == '`') {
      c = ' ';
    }
  }
  const std::size_t kMaxLen = 48;
  if (base.size() > kMaxLen) {
    base.resize(kMaxLen);
  }
  return base + " 插画 场景图";
}

void AttachImagesWithWanxiangAndUnsplash(const GenerationConfig& config,
                                         WanxiangImageClient* wanx_client,
                                         std::vector<SlideContent>& slides,
                                         std::uint64_t request_id,
                                         const std::string& topic) {
  for (std::size_t i = 0; i < slides.size(); ++i) {
    auto& slide = slides[i];
    if (slide.image_prompts.empty()) {
      if (!slide.title.empty()) {
        slide.image_prompts.push_back(slide.title + " 场景");
      } else if (!topic.empty()) {
        slide.image_prompts.push_back(topic + " 场景");
      }
    }
    if (slide.image_prompts.empty()) {
      continue;
    }

    bool has_any_image = !slide.image_paths.empty() || !slide.image_urls.empty();

    if (!has_any_image && wanx_client && wanx_client->IsEnabled()) {
      const auto safe_prompt = BuildSafeImagePrompt(slide, topic);
      if (safe_prompt.empty()) {
        continue;
      }
      std::vector<std::string> urls;
      std::string error;
      if (!wanx_client->GenerateImages(safe_prompt, urls, error)) {
        // 如果因内容巡检拒绝，退回到一个与主题弱相关的通用安全提示词，保证仍然有真实配图。
        if (error.find("DataInspectionFailed") != std::string::npos) {
          std::vector<std::string> generic_urls;
          std::string generic_error;
          const std::string generic_prompt = "简洁技术主题PPT 扁平插画 场景图";
          if (!wanx_client->GenerateImages(generic_prompt, generic_urls, generic_error)) {
            Logger::Warn("Wanxiang image generation failed: " + error +
                         " prompt=" + safe_prompt +
                         "; generic_error=" + generic_error);
          } else {
            urls = std::move(generic_urls);
          }
        } else {
          Logger::Warn("Wanxiang image generation failed: " + error + " prompt=" + safe_prompt);
        }
      }

      if (!urls.empty()) {
        const auto timeout = wanx_client->timeout_seconds();
        for (std::size_t j = 0; j < urls.size(); ++j) {
          const auto& url = urls[j];
          if (url.empty()) {
            continue;
          }
          const auto image_path = BuildImagePath(config, request_id, i, j);
          std::string save_error;
          bool saved = DownloadToFile(url, image_path, timeout, save_error);
          if (saved) {
            slide.image_paths.push_back(image_path);
            slide.image_urls.push_back(url);
            has_any_image = true;
          } else {
            Logger::Warn("Wanxiang image download failed: " + save_error);
          }
        }
      }
    }

    // 对仅有 URL 而没有本地路径的图片，统一在服务端预下载到 image_dir，
    // 这样无论是 Python 模板模式还是 Node 风格模式，都可以稳定插入本地图片文件。
    if (slide.image_paths.empty() && !slide.image_urls.empty()) {
      const auto& url = slide.image_urls.front();
      if (!url.empty()) {
        const auto image_path = BuildImagePath(config, request_id, i, 0);
        const std::uint32_t timeout =
            wanx_client && wanx_client->timeout_seconds() > 0 ? wanx_client->timeout_seconds() : 60;
        std::string save_error;
        if (DownloadToFile(url, image_path, timeout, save_error)) {
          slide.image_paths.push_back(image_path);
        } else {
          Logger::Warn("Fallback image download failed: " + save_error);
        }
      }
    }
  }
}

std::string BuildObjectKey(const GenerationConfig& config, const std::string& output_path) {
  if (output_path.empty()) {
    return {};
  }
  std::filesystem::path base_dir(config.output_dir);
  std::filesystem::path target_path(output_path);
  std::error_code ec;
  const auto base = std::filesystem::weakly_canonical(base_dir, ec);
  if (ec) {
    return target_path.filename().string();
  }
  const auto target = std::filesystem::weakly_canonical(target_path, ec);
  if (ec) {
    return target_path.filename().string();
  }
  std::filesystem::path relative;
  if (target.string().find(base.string()) == 0) {
    relative = std::filesystem::relative(target, base, ec);
  }
  if (ec || relative.empty()) {
    relative = target.filename();
  }
  auto key = relative.generic_string();
  if (key.empty()) {
    key = target.filename().string();
  }
  return key;
}

/** Object key for PDF: same relative path as pptx but .pdf extension. */
std::string BuildObjectKeyPdf(const GenerationConfig& config, const std::string& pptx_output_path) {
  if (pptx_output_path.empty()) {
    return {};
  }
  std::filesystem::path p(pptx_output_path);
  p.replace_extension(".pdf");
  return BuildObjectKey(config, p.string());
}

/** Ensure PDF exists at pdf_path; convert from pptx_path using soffice if needed. Returns true on success. */
bool EnsurePdfFromPptx(const std::string& pptx_path, const std::string& pdf_path,
                       const std::string& soffice_binary, std::string& error) {
  if (pptx_path.empty() || pdf_path.empty()) {
    error = "Empty path";
    return false;
  }
  std::error_code ec;
  if (std::filesystem::exists(pdf_path, ec) && std::filesystem::file_size(pdf_path, ec) > 0 && !ec) {
    return true;
  }
  if (!std::filesystem::exists(pptx_path, ec) || ec) {
    error = "PPTX file not found: " + pptx_path;
    return false;
  }
  std::filesystem::path out_path(pdf_path);
  std::string out_dir = out_path.parent_path().string();
  std::ostringstream cmd;
  cmd << "\"" << soffice_binary << "\" --headless --convert-to pdf --outdir \"" << out_dir << "\" \"" << pptx_path << "\"";
  const int ret = std::system(cmd.str().c_str());
  if (ret != 0) {
    error = "soffice convert failed with code " + std::to_string(ret);
    return false;
  }
  if (!std::filesystem::exists(pdf_path, ec) || std::filesystem::file_size(pdf_path, ec) == 0 || ec) {
    error = "PDF was not created: " + pdf_path;
    return false;
  }
  return true;
}

/** Map frontend style id to PptxGenJS theme preset (pptxgen_builder.js THEME_PRESETS). */
std::string StyleToThemePreset(const std::string& style) {
  static const std::unordered_map<std::string, std::string> kMap = {
      {"business", "midnight"}, {"academic", "forest"},
      {"creative", "coral"},    {"minimal", "charcoal"}};
  auto it = kMap.find(style);
  return (it != kMap.end()) ? it->second : "midnight";
}

/** Generate PPTX using PptxGenJS only (no template). Writes payload and runs node script. */
bool RunPptxGenFromPreset(const std::vector<SlideContent>& slides,
                         const std::string& output_path,
                         const std::string& style,
                         const std::string& options_json,
                         const GenerationConfig& config,
                         std::string& error) {
  if (config.node_binary.empty() || config.pptxgen_builder_script.empty()) {
    error = "PptxGenJS builder not configured";
    return false;
  }
  std::error_code ec;
  if (!std::filesystem::exists(config.pptxgen_builder_script, ec) || ec) {
    error = "PptxGenJS script not found";
    return false;
  }
  std::filesystem::path out_path(output_path);
  std::filesystem::path payload_path = out_path;
  payload_path.replace_extension(".json");
  std::filesystem::create_directories(out_path.parent_path(), ec);

  nlohmann::json payload;
  payload["themePreset"] = StyleToThemePreset(style);
  if (!options_json.empty()) {
    try {
      auto opts = nlohmann::json::parse(options_json);
      if (opts.contains("themePreset") && opts["themePreset"].is_string()) {
        payload["themePreset"] = opts["themePreset"].get<std::string>();
      }
    } catch (const std::exception&) {}
  }
  payload["slides"] = nlohmann::json::array();
  for (const auto& slide : slides) {
    nlohmann::json item;
    item["title"] = slide.title;
    if (!slide.bullets.empty()) {
      item["bullets"] = slide.bullets;
    } else if (!slide.raw_text.empty()) {
      item["bullets"] = nlohmann::json::array({slide.raw_text});
    } else {
      item["bullets"] = nlohmann::json::array();
    }
    if (!slide.bullet_groups.empty()) {
      item["bulletGroups"] = slide.bullet_groups;
    }
    if (!slide.notes.empty()) {
      item["notes"] = slide.notes;
    }
    if (!slide.image_paths.empty()) {
      item["imagePaths"] = slide.image_paths;
    }
    if (!slide.image_urls.empty()) {
      item["imageUrls"] = slide.image_urls;
    }
    if (!slide.layout_hint.empty()) {
      item["layoutHint"] = slide.layout_hint;
    }
    if (slide.chart_data.has_value()) {
      const auto& cd = slide.chart_data.value();
      nlohmann::json chart_json;
      chart_json["type"] = cd.type;
      chart_json["title"] = cd.title;
      nlohmann::json items_arr = nlohmann::json::array();
      for (const auto& cdi : cd.items) {
        items_arr.push_back({{"label", cdi.label}, {"value", cdi.value}});
      }
      chart_json["items"] = std::move(items_arr);
      item["chartData"] = std::move(chart_json);
    }
    payload["slides"].push_back(std::move(item));
  }

  std::ofstream out(payload_path.string());
  if (!out.is_open()) {
    error = "Cannot write payload file";
    return false;
  }
  out << payload.dump();
  out.close();
  if (!out.good()) {
    error = "Failed to write payload";
    return false;
  }

  std::ostringstream cmd;
  cmd << "\"" << config.node_binary << "\""
      << " \"" << config.pptxgen_builder_script << "\""
      << " --data-json \"" << payload_path.string() << "\""
      << " --output \"" << output_path << "\"";
  const int ret = std::system(cmd.str().c_str());
  if (ret != 0) {
    error = "PptxGenJS script failed with code " + std::to_string(ret);
    return false;
  }
  if (!std::filesystem::exists(output_path, ec) || std::filesystem::file_size(output_path, ec) == 0 || ec) {
    error = "PptxGenJS did not produce output file";
    return false;
  }
  return true;
}

std::string Trim(std::string value) {
  const auto start = value.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) {
    return {};
  }
  const auto end = value.find_last_not_of(" \t\r\n");
  return value.substr(start, end - start + 1);
}

std::string BuildDownloadFilename(const PptRequest& request, const User& user, const std::string& extension = ".pptx") {
  const auto safe_title = SanitizeFilenamePart(request.title, 80);
  const auto safe_email = SanitizeFilenamePart(user.email, 80);
  std::string filename = safe_title;
  if (!safe_email.empty()) {
    filename += "_" + safe_email;
  }
  filename += "_" + std::to_string(request.id);
  if (extension.empty()) {
    filename += ".pptx";
  } else if (extension[0] == '.') {
    filename += extension;
  } else {
    filename += "." + extension;
  }
  return filename;
}

void RemoveFileQuietly(const std::filesystem::path& path) {
  if (path.empty()) {
    return;
  }
  std::error_code ec;
  std::filesystem::remove(path, ec);
  if (ec) {
    Logger::Warn("Failed to remove file: " + path.string() + ", error=" + ec.message());
  }
}

bool IsUnderDirectory(const std::filesystem::path& base_dir,
                      const std::filesystem::path& target_path) {
  if (base_dir.empty() || target_path.empty()) {
    return false;
  }
  std::error_code ec;
  const auto base = std::filesystem::weakly_canonical(base_dir, ec);
  if (ec) {
    return false;
  }
  const auto target = std::filesystem::weakly_canonical(target_path, ec);
  if (ec) {
    return false;
  }
  auto base_it = base.begin();
  auto target_it = target.begin();
  for (; base_it != base.end() && target_it != target.end(); ++base_it, ++target_it) {
    if (*base_it != *target_it) {
      return false;
    }
  }
  return base_it == base.end();
}

struct ByteRange {
  std::uint64_t start = 0;
  std::uint64_t end = 0;
  bool valid = false;
};

ByteRange ParseRangeHeader(const std::string& header, std::uint64_t file_size) {
  ByteRange range;
  if (header.empty() || file_size == 0) {
    return range;
  }
  const std::string prefix = "bytes=";
  if (header.rfind(prefix, 0) != 0) {
    return range;
  }
  std::string spec = header.substr(prefix.size());
  const auto comma_pos = spec.find(',');
  if (comma_pos != std::string::npos) {
    spec = spec.substr(0, comma_pos);
  }
  const auto dash_pos = spec.find('-');
  if (dash_pos == std::string::npos) {
    return range;
  }
  const auto start_str = spec.substr(0, dash_pos);
  const auto end_str = spec.substr(dash_pos + 1);
  try {
    if (start_str.empty()) {
      if (end_str.empty()) {
        return range;
      }
      const auto suffix_len = static_cast<std::uint64_t>(std::stoull(end_str));
      if (suffix_len == 0) {
        return range;
      }
      const auto len = std::min<std::uint64_t>(suffix_len, file_size);
      range.start = file_size - len;
      range.end = file_size - 1;
      range.valid = true;
      return range;
    }

    range.start = static_cast<std::uint64_t>(std::stoull(start_str));
    if (range.start >= file_size) {
      return range;
    }
    if (end_str.empty()) {
      range.end = file_size - 1;
    } else {
      range.end = static_cast<std::uint64_t>(std::stoull(end_str));
      if (range.end >= file_size) {
        range.end = file_size - 1;
      }
    }
    if (range.end < range.start) {
      return range;
    }
    range.valid = true;
    return range;
  } catch (...) {
    return range;
  }
}

struct PptGenerationJob {
  PptRequest ppt_request;
  PptRequestInput input;
  std::uint64_t user_id = 0;
  std::string user_email;
  std::string template_id;
  std::shared_ptr<MaterialService> material_service;
};

std::filesystem::path BuildStructurePath(const GenerationConfig& config,
                                         const std::string& output_path) {
  if (output_path.empty()) {
    return {};
  }
  std::filesystem::path p(output_path);
  std::filesystem::path base_dir(config.output_dir);
  std::error_code ec;
  const auto canonical_base = std::filesystem::weakly_canonical(base_dir, ec);
  const auto canonical_target = std::filesystem::weakly_canonical(p, ec);
  if (ec) {
    return {};
  }
  if (!IsUnderDirectory(canonical_base, canonical_target)) {
    return {};
  }
  p.replace_extension(".structure.json");
  return p;
}

// 统一 schema: 将 SlideContent 列表转换为前端可编辑 JSON
nlohmann::json SlidesToEditableJson(const PptRequest& request,
                                    const std::vector<SlideContent>& slides) {
  nlohmann::json result;
  result["title"] = request.title;
  result["theme_id"] = request.style.empty() ? "midnight" : request.style;
  nlohmann::json slide_array = nlohmann::json::array();
  for (std::size_t i = 0; i < slides.size(); ++i) {
    const auto& s = slides[i];
    nlohmann::json item;
    item["id"] = "slide_" + std::to_string(i + 1);
    std::string layout = s.layout_hint.empty() ? "title_content" : s.layout_hint;
    item["layout"] = layout;
    item["title"] = s.title;
    item["subtitle"] = "";
    nlohmann::json content;
    content["bullets"] = s.bullets;
    std::string image_url;
    if (!s.image_urls.empty()) {
      image_url = s.image_urls.front();
    }
    content["image_url"] = image_url;
    content["notes"] = s.notes;
    // 序列化图表数据，保证编辑链路不丢失
    if (s.chart_data.has_value()) {
      const auto& cd = s.chart_data.value();
      nlohmann::json chart_json;
      chart_json["type"] = cd.type;
      chart_json["title"] = cd.title;
      nlohmann::json items_arr = nlohmann::json::array();
      for (const auto& it : cd.items) {
        nlohmann::json it_json;
        it_json["label"] = it.label;
        it_json["value"] = it.value;
        items_arr.push_back(std::move(it_json));
      }
      chart_json["items"] = std::move(items_arr);
      content["chart_data"] = std::move(chart_json);
    }
    item["content"] = std::move(content);
    slide_array.push_back(std::move(item));
  }
  result["slides"] = std::move(slide_array);
  nlohmann::json options;
  options["show_page_number"] = true;
  options["lang"] = "zh";
  result["options"] = std::move(options);
  return result;
}

// 将前端编辑后的 JSON 转换回 SlideContent 列表
bool EditableJsonToSlides(const nlohmann::json& data,
                          std::vector<SlideContent>& out_slides,
                          std::string& error) {
  if (!data.is_object()) {
    error = "结构数据格式不正确";
    return false;
  }
  auto it = data.find("slides");
  if (it == data.end() || !it->is_array()) {
    error = "缺少 slides 数组";
    return false;
  }
  const auto& slides = *it;
  if (slides.empty()) {
    error = "slides 不能为空";
    return false;
  }
  if (slides.size() > 100) {
    error = "slides 数量过多";
    return false;
  }

  std::vector<SlideContent> result;
  result.reserve(slides.size());
  for (const auto& item : slides) {
    if (!item.is_object()) {
      continue;
    }
    SlideContent slide;
    slide.title = item.value("title", "");
    if (slide.title.size() > 512) {
      slide.title.resize(512);
    }
    const auto& content = item.value("content", nlohmann::json::object());
    if (content.is_object()) {
      if (auto it_b = content.find("bullets"); it_b != content.end() && it_b->is_array()) {
        for (const auto& bullet : *it_b) {
          if (bullet.is_string()) {
            auto text = bullet.get<std::string>();
            if (!text.empty()) {
              if (text.size() > 1024) {
                text.resize(1024);
              }
              slide.bullets.push_back(std::move(text));
            }
          }
        }
      }
      std::string image_url = content.value("image_url", "");
      if (!image_url.empty()) {
        slide.image_urls.push_back(std::move(image_url));
      }
      slide.notes = content.value("notes", "");
      if (slide.notes.size() > 2048) {
        slide.notes.resize(2048);
      }
      // 反序列化图表数据
      if (auto it_cd = content.find("chart_data"); it_cd != content.end() && it_cd->is_object()) {
        ChartData cd;
        cd.type = it_cd->value("type", "bar");
        cd.title = it_cd->value("title", "");
        if (auto it_items = it_cd->find("items"); it_items != it_cd->end() && it_items->is_array()) {
          for (const auto& it_item : *it_items) {
            if (!it_item.is_object()) continue;
            ChartDataItem cdi;
            cdi.label = it_item.value("label", "");
            cdi.value = it_item.value("value", 0.0);
            if (!cdi.label.empty()) {
              cd.items.push_back(std::move(cdi));
            }
          }
        }
        if (cd.items.size() >= 2) {
          slide.chart_data = std::move(cd);
        }
      }
    }
    slide.layout_hint = item.value("layout", "title_content");
    // raw_text 简单拼一个，方便后续预览使用
    slide.raw_text = slide.title;
    for (const auto& b : slide.bullets) {
      slide.raw_text.append("\n").append(b);
    }
    result.push_back(std::move(slide));
  }
  out_slides = std::move(result);
  return true;
}

void DoActualGeneration(
    const PptGenerationJob& job,
    std::shared_ptr<PptService> ppt_svc,
    std::shared_ptr<TemplateService> template_svc,
    std::shared_ptr<QwenClient> qwen_client,
    std::shared_ptr<S3Client> s3_client,
    std::shared_ptr<WanxiangImageClient> wanx_client,
    GenerationConfig generation_config,
    std::shared_ptr<RedisClient> redis,
    int redis_ttl_ppt_status) {
  using namespace std::chrono;
  const auto start_time = steady_clock::now();
  const std::uint64_t request_id = job.ppt_request.id;
  PptMetrics::IncGenerationTotal();
  Logger::Info("generation_start request_id=" + std::to_string(request_id));

  const std::string progress_path = BuildProgressPath(generation_config, request_id);
  const std::string error_path = BuildErrorPath(generation_config, request_id);
  WriteProgress(progress_path, 5, "初始化", "正在准备生成环境...");

  // Redis 状态 key
  const std::string redis_status_key = "ppt:status:" + std::to_string(request_id);

  // 辅助 lambda：写 Redis 进度（Redis 不可用时静默忽略）
  const auto redis_set_progress = [&](const std::string& status,
                                      const std::string& progress,
                                      const std::string& stage) {
    if (!redis) return;
    redis->HMSet(redis_status_key, {
        {"status",   status},
        {"progress", progress},
        {"stage",    stage},
    });
  };

  // 标记处理中
  redis_set_progress("processing", "5", "init");

  const auto record_end = [&](bool success) {
    const auto elapsed_ms = duration_cast<milliseconds>(steady_clock::now() - start_time).count();
    PptMetrics::SetLastGenerationDurationMs(static_cast<std::uint64_t>(elapsed_ms));
    if (success) {
      PptMetrics::IncGenerationSuccess();
    } else {
      PptMetrics::IncGenerationFailed();
    }
    Logger::Info("generation_end request_id=" + std::to_string(request_id) +
                 " status=" + (success ? "completed" : "failed") +
                 " duration_ms=" + std::to_string(elapsed_ms));
  };

  const PptRequest& ppt_request = job.ppt_request;
  const PptRequestInput& input = job.input;
  std::string update_error;

  auto template_info_opt = template_svc->FindById(job.template_id);
  if (!template_info_opt) {
    WriteErrorReason(error_path, "模板文件不存在（id: " + job.template_id + "），请尝试更换其他模板后重新生成");
    ppt_svc->UpdateRequestOutput(ppt_request.id, job.user_id, "", "failed", update_error);
    if (redis) {
      redis->HMSet(redis_status_key, {{"status","failed"},{"progress","0"},{"stage","error"}});
      redis->Expire(redis_status_key, redis_ttl_ppt_status);
    }
    Logger::Error("DoActualGeneration: template not found " + job.template_id);
    record_end(false);
    return;
  }

  std::string template_prompt = template_info_opt->prompt.empty()
                                    ? template_info_opt->description
                                    : template_info_opt->prompt;
  std::optional<std::string> template_file = template_svc->GetLocalFile(template_info_opt->id);
  nlohmann::json template_analysis;
  bool has_template_analysis = false;
  if (template_file) {
    std::string analysis_error;
    if (EnsureTemplateAnalysis(generation_config, template_info_opt->id, *template_file,
                               template_analysis, analysis_error)) {
      has_template_analysis = true;
    }
  }

  if (!qwen_client || !qwen_client->IsEnabled()) {
    WriteErrorReason(error_path, "AI 服务（Qwen）未配置或不可用，请联系管理员检查 API Key 配置");
    ppt_svc->UpdateRequestOutput(ppt_request.id, job.user_id, "", "failed", update_error);
    if (redis) {
      redis->HMSet(redis_status_key, {{"status","failed"},{"progress","0"},{"stage","error"}});
      redis->Expire(redis_status_key, redis_ttl_ppt_status);
    }
    record_end(false);
    return;
  }

  std::vector<OutlineItem> outline = input.outline;
  if (!outline.empty() && static_cast<int>(outline.size()) > input.pages) {
    outline.resize(static_cast<std::size_t>(input.pages));
  }

  // Build material context string if material_id is provided (and collect data_mentions for chart validation)
  std::string material_context;
  std::vector<std::string> material_data_mentions;
  if (!input.material_id.empty() && job.material_service) {
    Material mat;
    std::string mat_error;
    if (job.material_service->GetMaterial(input.material_id, job.user_id, mat, mat_error)) {
      if (mat.status == "completed" && !mat.extract_result.empty()) {
        try {
          auto er = nlohmann::json::parse(mat.extract_result);
          std::ostringstream ctx;
          ctx << "以下是用户提供的参考材料的关键信息：\n";
          if (er.contains("title") && er["title"].is_string() && !er["title"].get<std::string>().empty()) {
            ctx << "  标题: " << er["title"].get<std::string>() << "\n";
          }
          if (er.contains("summary") && er["summary"].is_string() && !er["summary"].get<std::string>().empty()) {
            ctx << "  摘要: " << er["summary"].get<std::string>() << "\n";
          }
          if (er.contains("outline") && er["outline"].is_array() && !er["outline"].empty()) {
            ctx << "  原文大纲: ";
            for (std::size_t i = 0; i < er["outline"].size(); ++i) {
              if (i > 0) ctx << "；";
              ctx << er["outline"][i].get<std::string>();
            }
            ctx << "\n";
          }
          if (er.contains("key_points") && er["key_points"].is_array() && !er["key_points"].empty()) {
            ctx << "  核心论点: ";
            for (std::size_t i = 0; i < er["key_points"].size(); ++i) {
              if (i > 0) ctx << "；";
              ctx << er["key_points"][i].get<std::string>();
            }
            ctx << "\n";
          }
          if (er.contains("data_mentions") && er["data_mentions"].is_array() && !er["data_mentions"].empty()) {
            ctx << "  关键数据: ";
            for (std::size_t i = 0; i < er["data_mentions"].size(); ++i) {
              if (i > 0) ctx << "；";
              material_data_mentions.push_back(er["data_mentions"][i].get<std::string>());
              ctx << material_data_mentions.back();
            }
            ctx << "\n";
          }
          if (er.contains("keywords") && er["keywords"].is_array() && !er["keywords"].empty()) {
            ctx << "  关键词: ";
            for (std::size_t i = 0; i < er["keywords"].size(); ++i) {
              if (i > 0) ctx << "、";
              ctx << er["keywords"][i].get<std::string>();
            }
            ctx << "\n";
          }
          ctx << "\n【约束】生成 PPT 时：所有数字、比例、统计结果、实验结论必须仅来自以上参考材料，禁止编造或篡改。"
              << "若某页包含图表(chart_data)，图表的 items 必须全部来自以上「关键数据」，不得使用其他数字。";
          material_context = ctx.str();
          Logger::Info("DoActualGeneration: injecting material context for " + input.material_id);
        } catch (const std::exception& ex) {
          Logger::Warn("DoActualGeneration: failed to parse material extract_result: " + std::string(ex.what()));
        }
      }
    } else {
      Logger::Warn("DoActualGeneration: material not found: " + input.material_id + " err=" + mat_error);
    }
  }

  // Build enriched topic that includes material context
  const std::string enriched_topic = material_context.empty()
      ? input.topic
      : material_context + "\n请基于以上内容，为主题「" + input.topic + "」生成结构清晰的PPT大纲。";

  std::vector<SlideContent> slides;
  std::string qwen_error;
  bool generated = false;
  std::string layout_guide_json;

  // ai_native 链路由 AiNativePptService 自行生成内容，跳过此处的 Qwen 幻灯片生成
  const bool is_ai_native = (input.generate_mode == "ai_native");

  if (!is_ai_native) {
    if (outline.empty()) {
      WriteProgress(progress_path, 15, "生成大纲", "AI 正在分析主题，规划内容结构...");
      redis_set_progress("processing", "15", "outline");
      std::string outline_error;
      if (!qwen_client->GenerateOutline(enriched_topic, input.pages, template_prompt, outline, outline_error)) {
        Logger::Warn("PPT outline generation failed: " + outline_error);
      }
    }

    WriteProgress(progress_path, 30, "分析版式", "正在加载模板版式信息...");
    redis_set_progress("processing", "30", "layout");
    const int layout_slide_count = outline.empty()
                                       ? std::max(1, std::min(input.pages, 10))
                                       : static_cast<int>(outline.size());
    if (has_template_analysis && layout_slide_count > 0 && template_file) {
      std::string layout_error;
      if (!LoadLayoutGuide(generation_config, template_info_opt->id, *template_file,
                           layout_slide_count, template_prompt, template_analysis,
                           *qwen_client, layout_guide_json, layout_error)) {
        layout_guide_json.clear();
      }
    }

    WriteProgress(progress_path, 45, "生成内容", "AI 正在为每张幻灯片生成详细内容...");
    redis_set_progress("processing", "45", "slides");
    if (!outline.empty()) {
      if (qwen_client->GenerateSlidesFromOutlineWithLayout(enriched_topic, outline, input.include_images,
                                                           layout_guide_json, slides, qwen_error,
                                                           input.include_charts)) {
        generated = true;
      } else {
        // 带版式的大纲生成失败时，再尝试一次不带版式的大纲生成
        std::string slides_error;
        if (qwen_client->GenerateSlidesFromOutline(enriched_topic, outline, input.include_images,
                                                   slides, slides_error, input.include_charts)) {
          generated = true;
        } else {
          Logger::Warn("Qwen slides-from-outline fallback failed: " + slides_error);
        }
      }
    }
    if (!generated) {
      if (qwen_client->GenerateSlidesWithLayout(enriched_topic, input.pages, template_prompt,
                                                input.include_images, layout_guide_json,
                                                slides, qwen_error, input.include_charts)) {
        generated = true;
      }
    }

    if (!generated) {
      Logger::Warn("Qwen slide generation failed: " + qwen_error);
      std::string reason = "AI 内容生成失败";
      if (qwen_error.find("Timeout") != std::string::npos || qwen_error.find("timeout") != std::string::npos) {
        reason = "AI 服务请求超时，主题内容可能过长，请尝试减少页数或简化主题描述后重新生成";
      } else if (!qwen_error.empty()) {
        reason = "AI 内容生成失败：" + qwen_error;
      }
      WriteErrorReason(error_path, reason);
      ppt_svc->UpdateRequestOutput(ppt_request.id, job.user_id, "", "failed", update_error);
      if (redis) {
        redis->HMSet(redis_status_key, {{"status","failed"},{"progress","0"},{"stage","error"}});
        redis->Expire(redis_status_key, redis_ttl_ppt_status);
      }
      record_end(false);
      return;
    }
  } else {
    // ai_native 模式：内容由 AiNativePptService 生成，此处标记为已生成以跳过后续检查
    generated = true;
  }

  if (input.enable_section_slides && static_cast<int>(slides.size()) > input.section_slide_interval) {
    std::vector<SlideContent> with_sections;
    int section_num = 1;
    for (size_t i = 0; i < slides.size(); i++) {
      if (i > 0 && i % static_cast<size_t>(input.section_slide_interval) == 0) {
        SlideContent sec;
        sec.title = "第" + std::to_string(++section_num) + "部分";
        sec.layout_hint = "section";
        with_sections.push_back(sec);
      }
      with_sections.push_back(slides[i]);
    }
    slides = std::move(with_sections);
  }

  // 文献模式下校验图表数据：若某页 chart_data 中的数值/标签未在文献 data_mentions 中出现则清除该页图表
  if (!material_context.empty() && input.include_charts && !material_data_mentions.empty()) {
    for (auto& s : slides) {
      if (!s.chart_data.has_value() || s.chart_data->items.empty()) continue;
      const auto& cd = s.chart_data.value();
      bool any_unmatched = false;
      for (const auto& item : cd.items) {
        std::string val_str;
        if (item.value == static_cast<double>(static_cast<int>(item.value)))
          val_str = std::to_string(static_cast<int>(item.value));
        else
          val_str = std::to_string(item.value);
        bool found = false;
        for (const std::string& m : material_data_mentions) {
          if (m.find(val_str) != std::string::npos || (!item.label.empty() && m.find(item.label) != std::string::npos)) {
            found = true;
            break;
          }
        }
        if (!found) {
          any_unmatched = true;
          break;
        }
      }
      if (any_unmatched) {
        Logger::Warn("DoActualGeneration: 清除未在文献关键数据中匹配的图表，标题=" + s.title);
        s.chart_data = std::nullopt;
      }
    }
  }

  // ai_native 模式图片由 AiNativePptService 内部处理，此处跳过
  if (!is_ai_native && input.include_images) {
    WriteProgress(progress_path, 60, "配图生成", "正在为幻灯片搜索和生成配图...");
    redis_set_progress("processing", "60", "images");
    AttachImagesWithWanxiangAndUnsplash(generation_config, wanx_client.get(), slides, ppt_request.id, input.topic);
  }

  WriteProgress(progress_path, 80, "渲染文件", "正在将内容渲染为 PPT 文件...");
  redis_set_progress("processing", "80", "rendering");
  std::string output_path = BuildOutputPath(generation_config, ppt_request.id, input.title, job.user_email);
  Logger::Info("Generating PPT (async): " + output_path);
  std::string generate_error;
  bool gen_ok = false;

  if (input.generate_mode == "style") {
    nlohmann::json style_options;
    if (!input.theme_preset.empty()) {
      style_options["themePreset"] = input.theme_preset;
    }
    std::string options_json = style_options.empty() ? "" : style_options.dump();
    gen_ok = RunPptxGenFromPreset(slides, output_path, input.style, options_json, generation_config, generate_error);
  } else if (input.generate_mode == "ai_native") {
    // 链路 3：AI 原生生成——由 LLM 全权决策视觉设计
    AiNativeGenerationConfig ai_config;
    ai_config.node_binary = generation_config.node_binary;
    ai_config.ai_native_builder_script = generation_config.ai_native_builder_script;
    ai_config.output_dir = generation_config.output_dir;
    ai_config.image_dir = generation_config.image_dir;

    auto ai_svc = std::make_unique<AiNativePptService>(
        generation_config.qwen_api_key,
        generation_config.qwen_timeout_seconds * 2);  // 链路 3 需要更长超时

    // 进度回调：将 AiNativePptService 内部细粒度进度写入进度文件
    auto ai_progress_cb = [&progress_path](int prog, const std::string& stage, const std::string& step) {
      WriteProgress(progress_path, prog, stage, step);
    };

    gen_ok = ai_svc->Generate(
        input.topic,
        input.style,
        input.pages,
        input.ai_style_prompt,
        outline,
        output_path,
        ai_config,
        generate_error,
        input.include_images,
        input.include_charts,
        wanx_client.get(),
        ai_progress_cb,
        material_context);

    if (!gen_ok) {
      const std::string fallback_reason = generate_error;
      Logger::Warn("AiNative generation failed, falling back to style mode: " + fallback_reason);
      WriteProgress(progress_path, 40, "降级处理", "AI 原生生成遇到问题，切换到预设主题模式...");
      generate_error.clear();
      if (slides.empty()) {
        std::string outline_error;
        if (outline.empty()) {
          WriteProgress(progress_path, 45, "生成大纲", "AI 正在重新生成大纲...");
          qwen_client->GenerateOutline(enriched_topic, input.pages, template_prompt, outline, outline_error);
        }
        std::string slides_error;
        if (!outline.empty()) {
          WriteProgress(progress_path, 55, "生成内容", "AI 正在生成幻灯片内容...");
          qwen_client->GenerateSlidesFromOutline(enriched_topic, outline, input.include_images,
                                                 slides, slides_error, input.include_charts);
        }
        if (slides.empty()) {
          qwen_client->GenerateSlides(enriched_topic, input.pages, template_prompt,
                                      input.include_images, slides, slides_error);
        }
      }
      WriteProgress(progress_path, 75, "渲染文件", "正在将内容渲染为 PPT 文件...");
      nlohmann::json style_options;
      style_options["themePreset"] = "midnight";
      gen_ok = RunPptxGenFromPreset(slides, output_path, input.style,
                                    style_options.dump(), generation_config, generate_error);
      if (gen_ok) {
        const std::string warn_path = output_path + ".warn";
        std::ofstream wf(warn_path, std::ios::trunc);
        if (wf) {
          wf << "AI 原生生成失败，已自动降级为预设主题模式。原因：" << fallback_reason;
        }
      }
    }
  } else {
    if (!template_file) {
      Logger::Warn("Template file missing for id: " + template_info_opt->id);
      WriteErrorReason(error_path, "模板文件丢失（" + template_info_opt->id + "），该模板可能已被删除，请更换其他模板后重新生成");
      ppt_svc->UpdateRequestOutput(ppt_request.id, job.user_id, "", "failed", update_error);
      if (redis) {
        redis->HMSet(redis_status_key, {{"status","failed"},{"progress","0"},{"stage","error"}});
        redis->Expire(redis_status_key, redis_ttl_ppt_status);
      }
      record_end(false);
      return;
    }
    nlohmann::json template_options;
    template_options["builderMode"] = "template";
    if (!input.theme_preset.empty()) {
      template_options["themePreset"] = input.theme_preset;
    }
    std::string options_json = template_options.dump();
    gen_ok = ppt_svc->GeneratePptxFile(*template_file, slides, output_path, generate_error, layout_guide_json, options_json);
  }

  if (gen_ok) {
    WriteProgress(progress_path, 95, "收尾处理", "PPT 文件生成完成，正在进行最终处理...");
    redis_set_progress("processing", "95", "finishing");
    ppt_svc->UpdateRequestOutput(ppt_request.id, job.user_id, output_path, "completed", update_error);
    if (!outline.empty()) {
      AppendOutlineToPreviewJson(output_path, outline);
    }
    if (s3_client && s3_client->IsEnabled()) {
      const auto object_key = BuildObjectKey(generation_config, output_path);
      if (!object_key.empty()) {
        std::string upload_error;
        if (s3_client->UploadFile(output_path, object_key, upload_error)) {
          Logger::Info("S3 upload success: key=" + object_key);
        } else {
          Logger::Warn("S3 upload failed: " + upload_error);
        }
      }
    }
    std::string pdf_error;
    std::filesystem::path pdf_path(output_path);
    pdf_path.replace_extension(".pdf");
    const std::string pdf_path_str = pdf_path.string();
    if (EnsurePdfFromPptx(output_path, pdf_path_str, generation_config.soffice_binary, pdf_error)) {
      if (s3_client && s3_client->IsEnabled()) {
        const auto pdf_key = BuildObjectKeyPdf(generation_config, output_path);
        if (!pdf_key.empty()) {
          std::string upload_error;
          if (s3_client->UploadFile(pdf_path_str, pdf_key, upload_error)) {
            Logger::Info("S3 PDF upload success: key=" + pdf_key);
          } else {
            Logger::Warn("S3 PDF upload failed: " + upload_error);
          }
        }
      }
    } else {
      Logger::Warn("PDF generation skipped: " + pdf_error);
    }
    // Redis：写终态 completed，失效用户历史缓存
    if (redis) {
      redis->HMSet(redis_status_key, {
          {"status",   "completed"},
          {"progress", "100"},
          {"stage",    "done"},
      });
      redis->Expire(redis_status_key, redis_ttl_ppt_status);
      redis->Del("ppt:history:user:" + std::to_string(job.user_id));
    }
    // 生成完成，删除进度文件和错误文件
    std::error_code ec;
    std::filesystem::remove(progress_path, ec);
    std::filesystem::remove(error_path, ec);
    record_end(true);
  } else {
    Logger::Warn("PPTX generation failed: " + generate_error);
    std::string reason = "PPT 文件渲染失败";
    if (!generate_error.empty()) {
      reason = "PPT 文件渲染失败：" + generate_error;
    }
    WriteErrorReason(error_path, reason);
    ppt_svc->UpdateRequestOutput(ppt_request.id, job.user_id, "", "failed", update_error);
    // Redis：写终态 failed
    if (redis) {
      redis->HMSet(redis_status_key, {
          {"status",   "failed"},
          {"progress", "0"},
          {"stage",    "error"},
      });
      redis->Expire(redis_status_key, redis_ttl_ppt_status);
    }
    // 生成失败，删除进度文件
    std::error_code ec;
    std::filesystem::remove(progress_path, ec);
    record_end(false);
  }
}

}  // namespace

PptController::PptController(std::shared_ptr<AuthService> auth_service,
                           std::shared_ptr<PptService> ppt_service,
                           std::shared_ptr<ModelService> model_service,
                           std::shared_ptr<TemplateService> template_service,
                           GenerationConfig generation_config,
                           std::shared_ptr<QwenClient> qwen_client,
                           std::shared_ptr<S3Client> s3_client,
                           std::shared_ptr<WanxiangImageClient> wanx_client,
                           std::shared_ptr<ThreadPool> thread_pool,
                           std::shared_ptr<MaterialService> material_service,
                           std::shared_ptr<RedisClient> redis,
                           int redis_ttl_ppt_status,
                           int redis_ttl_ppt_history)
    : auth_service_(std::move(auth_service)),
      ppt_service_(std::move(ppt_service)),
      model_service_(std::move(model_service)),
      template_service_(std::move(template_service)),
      generation_config_(std::move(generation_config)),
      qwen_client_(std::move(qwen_client)),
      s3_client_(std::move(s3_client)),
      wanx_client_(std::move(wanx_client)),
      thread_pool_(std::move(thread_pool)),
      material_service_(std::move(material_service)),
      redis_(std::move(redis)),
      redis_ttl_ppt_status_(redis_ttl_ppt_status),
      redis_ttl_ppt_history_(redis_ttl_ppt_history) {}

HttpResponse PptController::Generate(const HttpRequest& request) {
  std::string error;
  auto user = Authenticate(request, error);
  if (!user) {
    return HttpResponse::Json(401, ErrorJson("ERR_UNAUTHORIZED", error.empty() ? "Unauthorized" : error));
  }

  auto model = model_service_->FindById("qwen-turbo");
  if (!model) {
    return HttpResponse::Json(500, ErrorJson("ERR_INTERNAL", kInternalErrorMessage));
  }

  try {
    if (request.body.find('\0') != std::string::npos) {
      return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "Invalid JSON"));
    }
    auto input = PptRequestInput::FromJson(nlohmann::json::parse(request.body));

    if (input.pages < 1) {
      input.pages = 1;
    } else if (input.pages > 50) {
      input.pages = 50;
    }

    if (input.title.empty() || input.topic.empty()) {
      return HttpResponse::Json(400, ErrorJson("ERR_PPT_TITLE_TOPIC_EMPTY", "Title and topic cannot be empty"));
    }

    std::string template_id;
    const bool use_style_only = (input.generate_mode == "style");
    if (use_style_only) {
      const auto& templates = template_service_->GetAll();
      if (!templates.empty()) {
        template_id = templates.front().id;
      }
    } else {
      if (auto it = request.query_params.find("template"); it != request.query_params.end() && !it->second.empty()) {
        template_id = it->second;
      } else if (!input.template_id.empty()) {
        template_id = input.template_id;
      }
    }

    std::optional<RemoteTemplate> template_info_opt;
    if (!template_id.empty()) {
      template_info_opt = template_service_->FindById(template_id);
    } else {
      const auto& templates = template_service_->GetAll();
      if (!templates.empty()) {
        template_info_opt = templates.front();
      }
    }

    if (!template_info_opt) {
      return HttpResponse::Json(400, ErrorJson("ERR_PPT_INVALID_TEMPLATE", "Invalid template"));
    }

    input.template_id = template_info_opt->id;
    std::string display_template_name = template_info_opt->name;
    if (use_style_only) {
      static const std::unordered_map<std::string, std::string> kStyleNames = {
          {"business", "商务"}, {"academic", "学术"}, {"creative", "创意"}, {"minimal", "简约"}};
      auto it = kStyleNames.find(input.style);
      const std::string style_label = (it != kStyleNames.end()) ? it->second : input.style;
      display_template_name = "预设主题: " + style_label;
    }

    PptRequest ppt_request;
    if (!ppt_service_->CreateRequest(input, user->id, model->name, display_template_name, ppt_request, error)) {
      Logger::Error(std::string("CreateRequest failed: ") + (error.empty() ? "Generation failed" : error));
      return HttpResponse::Json(500, ErrorJson("ERR_INTERNAL", kInternalErrorMessage));
    }

    PptGenerationJob job;
    job.ppt_request = ppt_request;
    job.input = input;
    job.user_id = user->id;
    job.user_email = user->email;
    job.template_id = template_info_opt->id;
    job.material_service = material_service_;

    // Redis：初始化生成状态 Hash
    if (redis_) {
      const std::string sk = "ppt:status:" + std::to_string(ppt_request.id);
      redis_->HMSet(sk, {
          {"status",   "queued"},
          {"progress", "0"},
          {"stage",    "init"},
      });
      redis_->Expire(sk, redis_ttl_ppt_status_);
      // 失效该用户的历史列表缓存（新任务入队后历史已变化）
      redis_->Del("ppt:history:user:" + std::to_string(user->id));
    }

    auto ppt_svc = ppt_service_;
    auto template_svc = template_service_;
    auto qwen = qwen_client_;
    auto s3 = s3_client_;
    auto wanx = wanx_client_;
    auto redis = redis_;
    int ttl_status = redis_ttl_ppt_status_;
    GenerationConfig config = generation_config_;
    thread_pool_->EnqueueDetached([job, ppt_svc, template_svc, qwen, s3, wanx, config,
                                   redis, ttl_status]() {
      DoActualGeneration(job, ppt_svc, template_svc, qwen, s3, wanx, config,
                         redis, ttl_status);
    });

    nlohmann::json payload{{"request", RequestToJson(ppt_request)}};
    return HttpResponse::Json(202, payload);
  } catch (const std::exception& ex) {
    Logger::Error(std::string("Failed to parse PPT request: ") + ex.what());
    return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "Invalid JSON"));
  }
}

HttpResponse PptController::GetRequestStatus(const HttpRequest& request) {
  std::string error;
  auto user = Authenticate(request, error);
  if (!user) {
    return HttpResponse::Json(401, ErrorJson("ERR_UNAUTHORIZED", error.empty() ? "Unauthorized" : error));
  }

  std::uint64_t request_id = 0;
  if (auto it = request.query_params.find("id"); it != request.query_params.end()) {
    request_id = ParseId(it->second);
  }
  if (request_id == 0) {
    return HttpResponse::Json(400, ErrorJson("ERR_PPT_INVALID_REQUEST_ID", "Invalid request ID"));
  }

  // ── Redis 优先读取生成状态 ──────────────────────────────────────────────
  // 对 pending/processing 状态的高频轮询，从 Redis Hash 直接返回进度，跳过 MySQL。
  // completed/failed/其他 状态仍需从 MySQL 取完整记录（含 output_path 等字段）。
  if (redis_) {
    const std::string sk = "ppt:status:" + std::to_string(request_id);
    auto fields = redis_->HGetAll(sk);
    if (!fields.empty()) {
      const std::string& cached_status = fields.count("status") ? fields.at("status") : "";
      if (cached_status == "queued" || cached_status == "processing") {
        // 直接用 Redis 中的进度数据构造轻量响应，不查 MySQL
        nlohmann::json req_json;
        req_json["id"]       = request_id;
        req_json["status"]   = cached_status;
        req_json["progress"] = fields.count("progress") ? fields.at("progress") : "0";
        req_json["stage"]    = fields.count("stage")    ? fields.at("stage")    : "";
        return HttpResponse::Json(200, nlohmann::json{{"request", req_json}});
      }
    }
  }

  // ── 缓存未命中或终态（completed/failed），查 MySQL 取完整记录 ──────────
  PptRequest ppt_request;
  if (!ppt_service_->GetRequest(user->id, request_id, ppt_request, error)) {
    return HttpResponse::Json(404, ErrorJson("ERR_PPT_REQUEST_NOT_FOUND", error.empty() ? "Request not found" : error));
  }

  std::string signed_url;
  std::string signed_url_pdf;
  if (s3_client_ && s3_client_->IsEnabled() && !ppt_request.output_path.empty()) {
    const auto object_key = BuildObjectKey(generation_config_, ppt_request.output_path);
    if (!object_key.empty()) {
      signed_url = s3_client_->PresignGetUrl(object_key);
    }
    const auto pdf_key = BuildObjectKeyPdf(generation_config_, ppt_request.output_path);
    if (!pdf_key.empty()) {
      signed_url_pdf = s3_client_->PresignGetUrl(pdf_key);
    }
  }
  nlohmann::json req_json = RequestToJson(ppt_request, signed_url, signed_url_pdf);

  // 若存在降级 warn 文件，附带到响应
  if (!ppt_request.output_path.empty()) {
    const std::string warn_path = ppt_request.output_path + ".warn";
    std::ifstream wf(warn_path);
    if (wf.good()) {
      std::string warn_msg((std::istreambuf_iterator<char>(wf)),
                            std::istreambuf_iterator<char>());
      if (!warn_msg.empty()) {
        req_json["warn"] = warn_msg;
      }
    }
  }

  // 若 Redis 中有进度信息且状态为中间态，补充进度字段（兼容无 Redis 场景）
  if (redis_ && (ppt_request.status == "pending" || ppt_request.status == "processing")) {
    const std::string sk = "ppt:status:" + std::to_string(request_id);
    auto fields = redis_->HGetAll(sk);
    if (!fields.empty()) {
      if (fields.count("progress")) req_json["progress"] = fields.at("progress");
      if (fields.count("stage"))    req_json["stage"]    = fields.at("stage");
    }
  }

  // 无 Redis 时降级：读取进度文件（原有逻辑保留）
  if (!redis_ && (ppt_request.status == "pending" || ppt_request.status == "processing")) {
    const std::string prog_path = BuildProgressPath(generation_config_, request_id);
    std::ifstream pf(prog_path);
    if (pf.good()) {
      try {
        std::string prog_str((std::istreambuf_iterator<char>(pf)),
                              std::istreambuf_iterator<char>());
        if (!prog_str.empty()) {
          auto prog_json = nlohmann::json::parse(prog_str);
          if (prog_json.contains("progress")) req_json["progress"] = prog_json["progress"];
          if (prog_json.contains("stage"))    req_json["stage"]    = prog_json["stage"];
          if (prog_json.contains("step"))     req_json["step"]     = prog_json["step"];
        }
      } catch (...) {}
    }
  }

  // 读取失败原因文件（仅 failed 状态有意义）
  if (ppt_request.status == "failed") {
    const std::string err_path = BuildErrorPath(generation_config_, request_id);
    std::ifstream ef(err_path);
    if (ef.good()) {
      std::string err_msg((std::istreambuf_iterator<char>(ef)),
                           std::istreambuf_iterator<char>());
      if (!err_msg.empty()) {
        req_json["errorReason"] = err_msg;
      }
    }
    if (!req_json.contains("errorReason")) {
      req_json["errorReason"] = "PPT 生成失败，请稍后重试";
    }
  }

  nlohmann::json payload{{"request", req_json}};
  return HttpResponse::Json(200, payload);
}

HttpResponse PptController::History(const HttpRequest& request) {
  std::string error;
  auto user = Authenticate(request, error);
  if (!user) {
    return HttpResponse::Json(401, ErrorJson("ERR_UNAUTHORIZED", error.empty() ? "Unauthorized" : error));
  }

  std::string query;
  if (auto it = request.query_params.find("q"); it != request.query_params.end()) {
    query = Trim(it->second);
  }

  auto list = ppt_service_->GetHistory(user->id, query, error);
  if (!error.empty()) {
    Logger::Error(std::string("GetHistory failed: ") + error);
    return HttpResponse::Json(500, ErrorJson("ERR_INTERNAL", kInternalErrorMessage));
  }

  nlohmann::json payload;
  payload["items"] = nlohmann::json::array();
  for (const auto& item : list) {
    std::string signed_url;
    std::string signed_url_pdf;
    if (s3_client_ && s3_client_->IsEnabled() && !item.output_path.empty()) {
      const auto object_key = BuildObjectKey(generation_config_, item.output_path);
      if (!object_key.empty()) {
        signed_url = s3_client_->PresignGetUrl(object_key);
      }
      const auto pdf_key = BuildObjectKeyPdf(generation_config_, item.output_path);
      if (!pdf_key.empty()) {
        signed_url_pdf = s3_client_->PresignGetUrl(pdf_key);
      }
    }
    payload["items"].push_back(RequestToJson(item, signed_url, signed_url_pdf));
  }

  return HttpResponse::Json(200, payload);
}

HttpResponse PptController::AdminHistory(const HttpRequest& request) {
  std::string error;
  auto user = Authenticate(request, error);
  if (!user) {
    return HttpResponse::Json(401, ErrorJson("ERR_UNAUTHORIZED", error.empty() ? "Unauthorized" : error));
  }
  if (!user->is_admin) {
    return HttpResponse::Json(403, ErrorJson("ERR_FORBIDDEN", "Forbidden"));
  }

  std::string query;
  if (auto it = request.query_params.find("q"); it != request.query_params.end()) {
    query = Trim(it->second);
  }

  auto list = ppt_service_->GetAdminHistory(query, error);
  if (!error.empty()) {
    Logger::Error(std::string("GetAdminHistory failed: ") + error);
    return HttpResponse::Json(500, ErrorJson("ERR_INTERNAL", kInternalErrorMessage));
  }

  nlohmann::json payload;
  payload["items"] = nlohmann::json::array();
  for (const auto& item : list) {
    std::string signed_url;
    std::string signed_url_pdf;
    if (s3_client_ && s3_client_->IsEnabled() && !item.output_path.empty()) {
      const auto object_key = BuildObjectKey(generation_config_, item.output_path);
      if (!object_key.empty()) {
        signed_url = s3_client_->PresignGetUrl(object_key);
      }
      const auto pdf_key = BuildObjectKeyPdf(generation_config_, item.output_path);
      if (!pdf_key.empty()) {
        signed_url_pdf = s3_client_->PresignGetUrl(pdf_key);
      }
    }
    payload["items"].push_back(RequestToJson(item, signed_url, signed_url_pdf));
  }

  return HttpResponse::Json(200, payload);
}

HttpResponse PptController::AdminMetrics(const HttpRequest& request) {
  std::string error;
  auto user = Authenticate(request, error);
  if (!user) {
    return HttpResponse::Json(401, ErrorJson("ERR_UNAUTHORIZED", error.empty() ? "Unauthorized" : error));
  }
  if (!user->is_admin) {
    return HttpResponse::Json(403, ErrorJson("ERR_FORBIDDEN", "Forbidden"));
  }

  std::string range = "week";
  if (auto it = request.query_params.find("range"); it != request.query_params.end()) {
    const auto value = Trim(it->second);
    if (value == "day" || value == "week" || value == "month") {
      range = value;
    }
  }

  PptService::AdminMetrics metrics;
  if (!ppt_service_->GetAdminMetrics(range, metrics, error)) {
    Logger::Error(std::string("GetAdminMetrics failed: ") + (error.empty() ? "获取统计数据失败" : error));
    return HttpResponse::Json(500, ErrorJson("ERR_INTERNAL", kInternalErrorMessage));
  }

  nlohmann::json payload;
  payload["summary"] = {
      {"total", metrics.total},
      {"success", metrics.success},
      {"failed", metrics.failed},
      {"successRate", metrics.success_rate},
      {"uniqueUsers", metrics.unique_users},
      {"templateCount", metrics.template_count},
  };
  payload["activity"] = {
      {"labels", metrics.activity_labels},
      {"values", metrics.activity_values},
  };
  payload["generation"] = {
      {"labels", metrics.generation_labels},
      {"series", nlohmann::json::array()},
  };
  for (const auto& series : metrics.generation_series) {
    payload["generation"]["series"].push_back({{"name", series.name}, {"values", series.values}});
  }
  payload["templateShare"] = nlohmann::json::array();
  for (size_t i = 0; i < metrics.template_labels.size(); ++i) {
    const int value = i < metrics.template_values.size() ? metrics.template_values[i] : 0;
    payload["templateShare"].push_back({{"name", metrics.template_labels[i]}, {"value", value}});
  }
  payload["successRate"] = {{"success", metrics.success}, {"failed", metrics.failed}};
  payload["region"] = {{"labels", metrics.region_labels}, {"values", metrics.region_values}};
  payload["moduleHeat"] = {{"labels", metrics.module_labels}, {"values", metrics.module_values}};
  payload["range"] = range;

  return HttpResponse::Json(200, payload);
}

HttpResponse PptController::AdminInsights(const HttpRequest& request) {
  std::string error;
  auto user = Authenticate(request, error);
  if (!user) {
    return HttpResponse::Json(401, ErrorJson("ERR_UNAUTHORIZED", error.empty() ? "Unauthorized" : error));
  }
  if (!user->is_admin) {
    return HttpResponse::Json(403, ErrorJson("ERR_FORBIDDEN", "Forbidden"));
  }

  PptService::InsightData data;
  if (!ppt_service_->GetInsights(data, error)) {
    Logger::Error(std::string("GetInsights failed: ") + error);
    return HttpResponse::Json(500, ErrorJson("ERR_INTERNAL", kInternalErrorMessage));
  }

  nlohmann::json payload;

  // 热门主题
  payload["topTopics"] = nlohmann::json::array();
  for (const auto& tk : data.top_topics) {
    payload["topTopics"].push_back({{"keyword", tk.keyword}, {"count", tk.count}});
  }

  // 模型使用分布
  payload["modelUsage"] = nlohmann::json::array();
  for (const auto& mu : data.model_usage) {
    payload["modelUsage"].push_back({{"model", mu.model}, {"count", mu.count}});
  }

  // 热力图
  payload["hourlyHeatmap"] = nlohmann::json::array();
  for (const auto& cell : data.hourly_heatmap) {
    payload["hourlyHeatmap"].push_back({{"hour", cell.hour}, {"weekday", cell.weekday}, {"count", cell.count}});
  }

  // 用户漏斗
  payload["userFunnel"] = {
    {"registered",       data.funnel_registered},
    {"generatedOnce",    data.funnel_generated_once},
    {"generatedMulti",   data.funnel_generated_multi}
  };

  // 页数分布
  payload["pagesDistribution"] = nlohmann::json::array();
  for (size_t i = 0; i < data.pages_labels.size(); ++i) {
    int v = i < data.pages_values.size() ? data.pages_values[i] : 0;
    payload["pagesDistribution"].push_back({{"label", data.pages_labels[i]}, {"value", v}});
  }

  return HttpResponse::Json(200, payload);
}

HttpResponse PptController::Outline(const HttpRequest& request) {
  std::string error;
  auto user = Authenticate(request, error);
  if (!user) {
    return HttpResponse::Json(401, ErrorJson("ERR_UNAUTHORIZED", error.empty() ? "Unauthorized" : error));
  }

  try {
    if (request.body.find('\0') != std::string::npos) {
      return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "Invalid JSON"));
    }
    auto input = PptRequestInput::FromJson(nlohmann::json::parse(request.body));
    if (input.pages < 1) {
      input.pages = 1;
    } else if (input.pages > 50) {
      input.pages = 50;
    }
    if (input.title.empty() || input.topic.empty()) {
      return HttpResponse::Json(400, ErrorJson("ERR_PPT_TITLE_TOPIC_EMPTY", "Title and topic cannot be empty"));
    }

    std::string template_id;
    if (!input.template_id.empty()) {
      template_id = input.template_id;
    }
    std::optional<RemoteTemplate> template_info_opt;
    if (!template_id.empty()) {
      template_info_opt = template_service_->FindById(template_id);
    } else {
      const auto& templates = template_service_->GetAll();
      if (!templates.empty()) {
        template_info_opt = templates.front();
      }
    }
    if (!template_info_opt) {
      return HttpResponse::Json(400, ErrorJson("ERR_PPT_INVALID_TEMPLATE", "Invalid template"));
    }
    std::string template_prompt = template_info_opt->prompt.empty()
                                      ? template_info_opt->description
                                      : template_info_opt->prompt;

    if (!qwen_client_ || !qwen_client_->IsEnabled()) {
      return HttpResponse::Json(500, ErrorJson("ERR_INTERNAL", kInternalErrorMessage));
    }

    // 若携带 material_id，构建与 DoActualGeneration 一致的 enriched_topic
    std::string enriched_topic = input.topic;
    if (!input.material_id.empty() && material_service_) {
      Material mat;
      std::string mat_error;
      if (material_service_->GetMaterial(input.material_id, user->id, mat, mat_error)) {
        if (mat.status == "completed" && !mat.extract_result.empty()) {
          try {
            auto er = nlohmann::json::parse(mat.extract_result);
            std::ostringstream ctx;
            ctx << "以下是用户提供的参考材料的关键信息：\n";
            if (er.contains("title") && er["title"].is_string() && !er["title"].get<std::string>().empty()) {
              ctx << "  标题: " << er["title"].get<std::string>() << "\n";
            }
            if (er.contains("summary") && er["summary"].is_string() && !er["summary"].get<std::string>().empty()) {
              ctx << "  摘要: " << er["summary"].get<std::string>() << "\n";
            }
            if (er.contains("outline") && er["outline"].is_array() && !er["outline"].empty()) {
              ctx << "  原文大纲: ";
              for (std::size_t i = 0; i < er["outline"].size(); ++i) {
                if (i > 0) ctx << "；";
                ctx << er["outline"][i].get<std::string>();
              }
              ctx << "\n";
            }
            if (er.contains("key_points") && er["key_points"].is_array() && !er["key_points"].empty()) {
              ctx << "  核心论点: ";
              for (std::size_t i = 0; i < er["key_points"].size(); ++i) {
                if (i > 0) ctx << "；";
                ctx << er["key_points"][i].get<std::string>();
              }
              ctx << "\n";
            }
            if (er.contains("data_mentions") && er["data_mentions"].is_array() && !er["data_mentions"].empty()) {
              ctx << "  关键数据: ";
              for (std::size_t i = 0; i < er["data_mentions"].size(); ++i) {
                if (i > 0) ctx << "；";
                ctx << er["data_mentions"][i].get<std::string>();
              }
              ctx << "\n";
            }
            if (er.contains("keywords") && er["keywords"].is_array() && !er["keywords"].empty()) {
              ctx << "  关键词: ";
              for (std::size_t i = 0; i < er["keywords"].size(); ++i) {
                if (i > 0) ctx << "、";
                ctx << er["keywords"][i].get<std::string>();
              }
              ctx << "\n";
            }
            ctx << "\n【约束】所有数据必须仅来自以上参考材料，禁止编造或篡改。";
            enriched_topic = ctx.str() + "\n请基于以上内容，为主题「" + input.topic + "」生成结构清晰的PPT大纲。";
            Logger::Info("Outline: injecting material context for " + input.material_id);
          } catch (const std::exception& ex) {
            Logger::Warn(std::string("Outline: failed to parse material extract_result: ") + ex.what());
          }
        }
      } else {
        Logger::Warn("Outline: material not found: " + input.material_id + " err=" + mat_error);
      }
    }

    std::vector<OutlineItem> outline;
    std::string outline_error;
    if (!qwen_client_->GenerateOutline(enriched_topic, input.pages, template_prompt, outline, outline_error)) {
      Logger::Error(std::string("GenerateOutline failed: ") + (outline_error.empty() ? "Outline generation failed" : outline_error));
      return HttpResponse::Json(500, ErrorJson("ERR_INTERNAL", kInternalErrorMessage));
    }

    nlohmann::json payload;
    payload["outline"] = OutlineToJson(outline);
    payload["count"] = outline.size();
    return HttpResponse::Json(200, payload);
  } catch (const std::exception& ex) {
    Logger::Error(std::string("Failed to parse outline request: ") + ex.what());
    return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "Invalid JSON"));
  }
}

HttpResponse PptController::Delete(const HttpRequest& request) {
  std::string error;
  auto user = Authenticate(request, error);
  if (!user) {
    return HttpResponse::Json(401, ErrorJson("ERR_UNAUTHORIZED", error.empty() ? "Unauthorized" : error));
  }

  std::uint64_t request_id = 0;
  if (auto it = request.query_params.find("id"); it != request.query_params.end()) {
    request_id = ParseId(it->second);
  }
  if (request_id == 0 && !request.body.empty() && request.body.find('\0') == std::string::npos) {
    try {
      const auto body = nlohmann::json::parse(request.body);
      if (body.contains("id")) {
        if (body["id"].is_number_unsigned()) {
          request_id = body["id"].get<std::uint64_t>();
        } else if (body["id"].is_string()) {
          request_id = ParseId(body["id"].get<std::string>());
        }
      }
    } catch (...) {
      // ignore body parse errors
    }
  }

  if (request_id == 0) {
    return HttpResponse::Json(400, ErrorJson("ERR_PPT_INVALID_REQUEST_ID", "Invalid request ID"));
  }

  PptRequest ppt_request;
  if (!ppt_service_->GetRequest(user->id, request_id, ppt_request, error)) {
    return HttpResponse::Json(404, ErrorJson("ERR_PPT_REQUEST_NOT_FOUND", error.empty() ? "Request not found" : error));
  }

  if (s3_client_ && s3_client_->IsEnabled() && !ppt_request.output_path.empty()) {
    const auto object_key = BuildObjectKey(generation_config_, ppt_request.output_path);
    if (!object_key.empty()) {
      std::string delete_error;
      if (!s3_client_->DeleteObject(object_key, delete_error)) {
        Logger::Warn("S3 delete failed: key=" + object_key + " error=" + delete_error);
        return HttpResponse::Json(500, ErrorJson("ERR_INTERNAL", kInternalErrorMessage));
      }
      Logger::Info("S3 delete success: key=" + object_key);
    }
    const auto pdf_key = BuildObjectKeyPdf(generation_config_, ppt_request.output_path);
    if (!pdf_key.empty()) {
      std::string delete_error;
      if (!s3_client_->DeleteObject(pdf_key, delete_error)) {
        Logger::Warn("S3 PDF delete failed: key=" + pdf_key + " error=" + delete_error);
      } else {
        Logger::Info("S3 PDF delete success: key=" + pdf_key);
      }
    }
  }

  if (!ppt_service_->DeleteRequest(user->id, request_id, error)) {
    if (error == "记录不存在或已删除") {
      return HttpResponse::Json(404, ErrorJson("ERR_PPT_REQUEST_NOT_FOUND", error));
    }
    return HttpResponse::Json(400, ErrorJson("ERR_PPT_DELETE_FAILED", error.empty() ? "Deletion failed" : error));
  }

  // Redis：清除状态 Hash 和用户历史列表缓存
  if (redis_) {
    redis_->Del("ppt:status:" + std::to_string(request_id));
    redis_->Del("ppt:history:user:" + std::to_string(user->id));
  }

  if (!ppt_request.output_path.empty()) {
    const std::filesystem::path output_path(ppt_request.output_path);
    const std::filesystem::path base_dir(generation_config_.output_dir);
    if (!IsUnderDirectory(base_dir, output_path)) {
      Logger::Warn("Refusing to delete file outside generated directory: " + output_path.string());
    } else {
      RemoveFileQuietly(output_path);
      std::filesystem::path template_copy(output_path);
      template_copy.replace_extension(".template.pptx");
      RemoveFileQuietly(template_copy);
      std::filesystem::path payload_json(output_path);
      payload_json.replace_extension(".json");
      RemoveFileQuietly(payload_json);
      std::filesystem::path pdf_path(output_path);
      pdf_path.replace_extension(".pdf");
      RemoveFileQuietly(pdf_path);
    }
  }

  return HttpResponse::Json(200, {{"message", "Request deleted successfully"}});
}

HttpResponse PptController::BatchDelete(const HttpRequest& request) {
  std::string error;
  auto user = Authenticate(request, error);
  if (!user) {
    return HttpResponse::Json(401, ErrorJson("ERR_UNAUTHORIZED", error.empty() ? "Unauthorized" : error));
  }

  // 解析 body: {"ids": [1, 2, 3]}
  if (request.body.empty()) {
    return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "Missing request body"));
  }
  nlohmann::json body;
  try {
    body = nlohmann::json::parse(request.body);
  } catch (...) {
    return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "Invalid JSON body"));
  }
  if (!body.contains("ids") || !body["ids"].is_array() || body["ids"].empty()) {
    return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "ids must be a non-empty array"));
  }

  const auto& ids_json = body["ids"];
  constexpr std::size_t kMaxBatch = 50;
  if (ids_json.size() > kMaxBatch) {
    return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "单次最多批量删除 50 条"));
  }

  std::vector<std::uint64_t> ids;
  ids.reserve(ids_json.size());
  for (const auto& v : ids_json) {
    std::uint64_t id = 0;
    if (v.is_number_unsigned()) id = v.get<std::uint64_t>();
    else if (v.is_number_integer()) id = static_cast<std::uint64_t>(v.get<std::int64_t>());
    else if (v.is_string()) id = ParseId(v.get<std::string>());
    if (id == 0) {
      return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "ids contains invalid value"));
    }
    ids.push_back(id);
  }

  nlohmann::json results = nlohmann::json::array();
  int success_count = 0;

  for (const auto request_id : ids) {
    nlohmann::json item;
    item["id"] = request_id;

    PptRequest ppt_request;
    std::string item_error;
    if (!ppt_service_->GetRequest(user->id, request_id, ppt_request, item_error)) {
      item["status"] = "error";
      item["message"] = item_error.empty() ? "记录不存在" : item_error;
      results.push_back(item);
      continue;
    }

    // 删除 S3 对象
    if (s3_client_ && s3_client_->IsEnabled() && !ppt_request.output_path.empty()) {
      const auto object_key = BuildObjectKey(generation_config_, ppt_request.output_path);
      if (!object_key.empty()) {
        std::string del_err;
        if (!s3_client_->DeleteObject(object_key, del_err)) {
          Logger::Warn("BatchDelete S3 pptx failed: key=" + object_key + " err=" + del_err);
        }
      }
      const auto pdf_key = BuildObjectKeyPdf(generation_config_, ppt_request.output_path);
      if (!pdf_key.empty()) {
        std::string del_err;
        s3_client_->DeleteObject(pdf_key, del_err);
      }
    }

    // 删除数据库记录
    if (!ppt_service_->DeleteRequest(user->id, request_id, item_error)) {
      item["status"] = "error";
      item["message"] = item_error.empty() ? "删除失败" : item_error;
      results.push_back(item);
      continue;
    }

    // 清理 Redis 缓存
    if (redis_) {
      redis_->Del("ppt:status:" + std::to_string(request_id));
    }

    // 删除本地文件
    if (!ppt_request.output_path.empty()) {
      const std::filesystem::path output_path(ppt_request.output_path);
      const std::filesystem::path base_dir(generation_config_.output_dir);
      if (IsUnderDirectory(base_dir, output_path)) {
        RemoveFileQuietly(output_path);
        std::filesystem::path template_copy(output_path);
        template_copy.replace_extension(".template.pptx");
        RemoveFileQuietly(template_copy);
        std::filesystem::path payload_json(output_path);
        payload_json.replace_extension(".json");
        RemoveFileQuietly(payload_json);
        std::filesystem::path pdf_path(output_path);
        pdf_path.replace_extension(".pdf");
        RemoveFileQuietly(pdf_path);
      }
    }

    item["status"] = "ok";
    item["message"] = "deleted";
    results.push_back(item);
    ++success_count;
  }

  // 批量清除历史缓存（一次）
  if (redis_ && success_count > 0) {
    redis_->Del("ppt:history:user:" + std::to_string(user->id));
  }

  const int total = static_cast<int>(ids.size());
  const int failed = total - success_count;
  const int status_code = (failed == 0) ? 200 : (success_count == 0 ? 400 : 207);
  return HttpResponse::Json(status_code, {
    {"success", success_count},
    {"failed", failed},
    {"total", total},
    {"results", results}
  });
}

HttpResponse PptController::Download(const HttpRequest& request) {
  std::string error;
  auto user = Authenticate(request, error);
  if (!user) {
    const auto token = ExtractToken(request);
    Logger::Warn("PPT download unauthorized: method=" + request.method +
                 " path=" + request.target +
                 " has_token=" + std::string(token.empty() ? "0" : "1") +
                 " error=" + (error.empty() ? "unknown" : error));
    return HttpResponse::Json(401, ErrorJson("ERR_UNAUTHORIZED", error.empty() ? "Unauthorized" : error));
  }
  const bool is_head = request.method == "HEAD" || request.method == "head";

  std::uint64_t request_id = 0;
  if (auto it = request.query_params.find("id"); it != request.query_params.end()) {
    request_id = ParseId(it->second);
  }
  if (request_id == 0) {
    return HttpResponse::Json(400, ErrorJson("ERR_PPT_INVALID_REQUEST_ID", "Invalid request ID"));
  }

  PptRequest ppt_request;
  if (!ppt_service_->GetRequest(user->id, request_id, ppt_request, error)) {
    Logger::Warn("PPT download request not found: user_id=" + std::to_string(user->id) +
                 " request_id=" + std::to_string(request_id));
    return HttpResponse::Json(404, ErrorJson("ERR_PPT_REQUEST_NOT_FOUND", error.empty() ? "Request not found" : error));
  }
  if (ppt_request.output_path.empty()) {
    Logger::Warn("PPT download missing output_path: user_id=" + std::to_string(user->id) +
                 " request_id=" + std::to_string(request_id));
    return HttpResponse::Json(404, ErrorJson("ERR_PPT_FILE_NOT_GENERATED", "PPT file not generated"));
  }

  const bool want_pdf = [&]() {
    auto it = request.query_params.find("format");
    if (it == request.query_params.end()) return false;
    std::string v = it->second;
    for (auto& c : v) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return v == "pdf";
  }();

  if (want_pdf) {
    std::filesystem::path pdf_path(ppt_request.output_path);
    pdf_path.replace_extension(".pdf");
    const std::string pdf_path_str = pdf_path.string();
    if (!EnsurePdfFromPptx(ppt_request.output_path, pdf_path_str, generation_config_.soffice_binary, error)) {
      Logger::Warn("PDF conversion failed: " + error);
      return HttpResponse::Json(503, ErrorJson("ERR_PDF_CONVERSION_FAILED", "PDF generation failed, please try again or download PPTX"));
    }
    if (s3_client_ && s3_client_->IsEnabled()) {
      const auto pdf_key = BuildObjectKeyPdf(generation_config_, ppt_request.output_path);
      if (!pdf_key.empty()) {
        std::string upload_error;
        if (s3_client_->UploadFile(pdf_path_str, pdf_key, upload_error)) {
          Logger::Info("S3 PDF upload on demand: key=" + pdf_key);
        }
      }
    }
    std::error_code size_ec;
    const auto file_size = std::filesystem::file_size(pdf_path_str, size_ec);
    if (size_ec || file_size == 0) {
      return HttpResponse::Json(404, ErrorJson("ERR_PPT_FILE_MISSING", "PDF file is missing"));
    }
    const std::string filename = BuildDownloadFilename(ppt_request, *user, ".pdf");
    if (is_head) {
      HttpResponse response;
      response.status_code = 200;
      response.status_message = "OK";
      response.headers["content-length"] = std::to_string(file_size);
      response.headers["content-type"] = "application/pdf";
      response.headers["content-disposition"] = "attachment; filename=\"" + filename + "\"";
      response.body.clear();
      return response;
    }
    std::ifstream input(pdf_path_str, std::ios::binary);
    if (!input.is_open()) {
      return HttpResponse::Json(404, ErrorJson("ERR_PPT_FILE_MISSING", "PDF file is missing"));
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    HttpResponse response;
    response.status_code = 200;
    response.status_message = "OK";
    response.headers["content-type"] = "application/pdf";
    response.headers["content-disposition"] = "attachment; filename=\"" + filename + "\"";
    response.headers["content-length"] = std::to_string(buffer.str().size());
    response.body = buffer.str();
    return response;
  }

  std::error_code size_ec;
  const auto file_size = std::filesystem::file_size(ppt_request.output_path, size_ec);
  if (size_ec || file_size == 0) {
    Logger::Warn("PPT download file missing: path=" + ppt_request.output_path +
                 " error=" + size_ec.message());
    return HttpResponse::Json(404, ErrorJson("ERR_PPT_FILE_MISSING", "PPT file is missing"));
  }

  const auto range_header = request.Header("range");
  const auto range = ParseRangeHeader(range_header, file_size);
  Logger::Info("PPT download request: method=" + request.method +
               " user_id=" + std::to_string(user->id) +
               " request_id=" + std::to_string(request_id) +
               " range=" + (range_header.empty() ? "none" : range_header) +
               " size=" + std::to_string(file_size));

  if (is_head) {
    HttpResponse response;
    if (!range_header.empty() && !range.valid) {
      response.status_code = 416;
      response.status_message = "Range Not Satisfiable";
      response.headers["content-range"] = "bytes */" + std::to_string(file_size);
    } else if (range.valid) {
      response.status_code = 206;
      response.status_message = "Partial Content";
      response.headers["content-range"] =
          "bytes " + std::to_string(range.start) + "-" + std::to_string(range.end) + "/" +
          std::to_string(file_size);
      response.headers["content-length"] = std::to_string(range.end - range.start + 1);
    } else {
      response.status_code = 200;
      response.status_message = "OK";
      response.headers["content-length"] = std::to_string(file_size);
    }
    response.headers["content-type"] = "application/vnd.openxmlformats-officedocument.presentationml.presentation";
    std::string filename;
    if (!ppt_request.output_path.empty()) {
      std::filesystem::path stored_path(ppt_request.output_path);
      filename = stored_path.filename().string();
    }
    if (filename.empty()) {
      filename = BuildDownloadFilename(ppt_request, *user);
    }
    response.headers["content-disposition"] =
        std::string("inline") + "; filename=\"" + filename + "\"";
    response.headers["accept-ranges"] = "bytes";
    response.body.clear();
    return response;
  }

  if (!range_header.empty() && !range.valid) {
    HttpResponse response;
    response.status_code = 416;
    response.status_message = "Range Not Satisfiable";
    response.headers["content-range"] = "bytes */" + std::to_string(file_size);
    response.headers["accept-ranges"] = "bytes";
    response.body.clear();
    return response;
  }

  std::ifstream input(ppt_request.output_path, std::ios::binary);
  if (!input.is_open()) {
    return HttpResponse::Json(404, ErrorJson("ERR_PPT_FILE_MISSING", "PPT file is missing"));
  }

  std::string body;
  if (range.valid) {
    const auto length = static_cast<std::size_t>(range.end - range.start + 1);
    body.resize(length);
    input.seekg(static_cast<std::streamoff>(range.start), std::ios::beg);
    input.read(body.data(), static_cast<std::streamsize>(length));
    body.resize(static_cast<std::size_t>(input.gcount()));
  } else {
    std::ostringstream buffer;
    buffer << input.rdbuf();
    body = buffer.str();
  }

  HttpResponse response;
  if (range.valid) {
    response.status_code = 206;
    response.status_message = "Partial Content";
    response.headers["content-range"] =
        "bytes " + std::to_string(range.start) + "-" + std::to_string(range.end) + "/" +
        std::to_string(file_size);
  } else {
    response.status_code = 200;
    response.status_message = "OK";
  }
  response.headers["content-type"] = "application/vnd.openxmlformats-officedocument.presentationml.presentation";
  const bool inline_view = request.query_params.find("inline") != request.query_params.end();
  std::string filename;
  if (!ppt_request.output_path.empty()) {
    std::filesystem::path stored_path(ppt_request.output_path);
    filename = stored_path.filename().string();
  }
  if (filename.empty()) {
    filename = BuildDownloadFilename(ppt_request, *user);
  }
  response.headers["content-disposition"] =
      std::string(inline_view ? "inline" : "attachment") +
      "; filename=\"" + filename + "\"";
  response.headers["accept-ranges"] = "bytes";
  response.body = std::move(body);
  return response;
}

HttpResponse PptController::Preview(const HttpRequest& request) {
  std::string error;
  auto user = Authenticate(request, error);
  if (!user) {
    return HttpResponse::Json(401, ErrorJson("ERR_UNAUTHORIZED", error.empty() ? "Unauthorized" : error));
  }

  std::uint64_t request_id = 0;
  if (auto it = request.query_params.find("id"); it != request.query_params.end()) {
    request_id = ParseId(it->second);
  }
  if (request_id == 0) {
    return HttpResponse::Json(400, ErrorJson("ERR_PPT_INVALID_REQUEST_ID", "Invalid request ID"));
  }

  PptRequest ppt_request;
  if (!ppt_service_->GetRequest(user->id, request_id, ppt_request, error)) {
    return HttpResponse::Json(404, ErrorJson("ERR_PPT_REQUEST_NOT_FOUND", error.empty() ? "Request not found" : error));
  }
  if (ppt_request.output_path.empty()) {
    return HttpResponse::Json(404, ErrorJson("ERR_PPT_FILE_NOT_GENERATED", "PPT file not generated"));
  }

  std::filesystem::path output_path(ppt_request.output_path);
  std::filesystem::path preview_path = output_path;
  preview_path.replace_extension(".json");

  const std::filesystem::path base_dir(generation_config_.output_dir);
  if (!IsUnderDirectory(base_dir, preview_path)) {
    Logger::Warn("Refusing to read preview outside generated directory: " + preview_path.string());
    return HttpResponse::Json(403, ErrorJson("ERR_PPT_PREVIEW_FORBIDDEN", "Preview not accessible"));
  }

  std::ifstream input(preview_path);
  if (!input.is_open()) {
    return HttpResponse::Json(404, ErrorJson("ERR_PPT_PREVIEW_NOT_FOUND", "Preview data not found"));
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();

  HttpResponse response;
  response.status_code = 200;
  response.status_message = "OK";
  response.headers["content-type"] = "application/json";
  response.body = buffer.str();
  return response;
}

HttpResponse PptController::GetStructure(const HttpRequest& request) {
  std::string error;
  auto user = Authenticate(request, error);
  if (!user) {
    return HttpResponse::Json(401, ErrorJson("ERR_UNAUTHORIZED", error.empty() ? "Unauthorized" : error));
  }

  std::uint64_t request_id = 0;
  if (auto it = request.query_params.find("id"); it != request.query_params.end()) {
    request_id = ParseId(it->second);
  }
  if (request_id == 0) {
    return HttpResponse::Json(400, ErrorJson("ERR_PPT_INVALID_REQUEST_ID", "Invalid request ID"));
  }

  PptRequest ppt_request;
  if (!ppt_service_->GetRequest(user->id, request_id, ppt_request, error)) {
    return HttpResponse::Json(404, ErrorJson("ERR_PPT_REQUEST_NOT_FOUND", error.empty() ? "Request not found" : error));
  }
  if (ppt_request.output_path.empty()) {
    return HttpResponse::Json(404, ErrorJson("ERR_PPT_FILE_NOT_GENERATED", "PPT file not generated"));
  }

  const auto structure_path = BuildStructurePath(generation_config_, ppt_request.output_path);
  nlohmann::json structure_json;
  if (!structure_path.empty()) {
    ReadJsonFile(structure_path, structure_json);
  }

  if (!structure_json.is_object() || !structure_json.contains("slides")) {
    // 没有结构文件时，退回到预览 JSON，把 SlideContent 适配成结构 JSON
    std::filesystem::path output_path(ppt_request.output_path);
    std::filesystem::path preview_path = output_path;
    preview_path.replace_extension(".json");
    nlohmann::json preview_json;
    if (ReadJsonFile(preview_path, preview_json) && preview_json.contains("slides")) {
      const auto& slides_json = preview_json["slides"];
      std::vector<SlideContent> slides;
      if (slides_json.is_array()) {
        for (const auto& s : slides_json) {
          SlideContent sc;
          sc.title = s.value("title", "");
          if (auto it_b = s.find("bullets"); it_b != s.end() && it_b->is_array()) {
            for (const auto& b : *it_b) {
              if (b.is_string()) {
                sc.bullets.push_back(b.get<std::string>());
              }
            }
          }
          if (auto it_u = s.find("imageUrls"); it_u != s.end() && it_u->is_array()) {
            for (const auto& u : *it_u) {
              if (u.is_string()) {
                sc.image_urls.push_back(u.get<std::string>());
              }
            }
          }
          sc.notes = s.value("notes", "");
          sc.layout_hint = s.value("layoutHint", "");
          sc.raw_text = s.value("rawText", "");
          slides.push_back(std::move(sc));
        }
      }
      structure_json = SlidesToEditableJson(ppt_request, slides);
    } else {
      // 再没有预览就给一个空的结构骨架
      std::vector<SlideContent> slides;
      slides.push_back(SlideContent{ppt_request.title, {}, {}, {}, {}, {}, {}, {}, "title_content", {}, std::nullopt});
      structure_json = SlidesToEditableJson(ppt_request, slides);
    }
  }

  return HttpResponse::Json(200, structure_json);
}

HttpResponse PptController::UpdateStructure(const HttpRequest& request) {
  std::string error;
  auto user = Authenticate(request, error);
  if (!user) {
    return HttpResponse::Json(401, ErrorJson("ERR_UNAUTHORIZED", error.empty() ? "Unauthorized" : error));
  }
  if (request.body.empty() || request.body.find('\0') != std::string::npos) {
    return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "Invalid JSON"));
  }

  std::uint64_t request_id = 0;
  if (auto it = request.query_params.find("id"); it != request.query_params.end()) {
    request_id = ParseId(it->second);
  }
  if (request_id == 0) {
    return HttpResponse::Json(400, ErrorJson("ERR_PPT_INVALID_REQUEST_ID", "Invalid request ID"));
  }

  PptRequest ppt_request;
  if (!ppt_service_->GetRequest(user->id, request_id, ppt_request, error)) {
    return HttpResponse::Json(404, ErrorJson("ERR_PPT_REQUEST_NOT_FOUND", error.empty() ? "Request not found" : error));
  }
  if (ppt_request.output_path.empty()) {
    return HttpResponse::Json(404, ErrorJson("ERR_PPT_FILE_NOT_GENERATED", "PPT file not generated"));
  }

  try {
    auto data = nlohmann::json::parse(request.body);
    const auto structure_path = BuildStructurePath(generation_config_, ppt_request.output_path);
    if (structure_path.empty()) {
      return HttpResponse::Json(500, ErrorJson("ERR_PPT_STRUCTURE_PATH", "Cannot determine structure path"));
    }
    if (!WriteJsonFile(structure_path, data)) {
      return HttpResponse::Json(500, ErrorJson("ERR_PPT_STRUCTURE_WRITE_FAILED", "Failed to write structure file"));
    }
    return HttpResponse::Json(200, {{"message", "ok"}});
  } catch (const std::exception& ex) {
    Logger::Error(std::string("Failed to parse PPT structure: ") + ex.what());
    return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "Invalid JSON"));
  }
}

HttpResponse PptController::RegenerateFromStructure(const HttpRequest& request) {
  std::string error;
  auto user = Authenticate(request, error);
  if (!user) {
    return HttpResponse::Json(401, ErrorJson("ERR_UNAUTHORIZED", error.empty() ? "Unauthorized" : error));
  }

  std::uint64_t request_id = 0;
  if (auto it = request.query_params.find("id"); it != request.query_params.end()) {
    request_id = ParseId(it->second);
  }
  if (request_id == 0) {
    return HttpResponse::Json(400, ErrorJson("ERR_PPT_INVALID_REQUEST_ID", "Invalid request ID"));
  }

  PptRequest ppt_request;
  if (!ppt_service_->GetRequest(user->id, request_id, ppt_request, error)) {
    return HttpResponse::Json(404, ErrorJson("ERR_PPT_REQUEST_NOT_FOUND", error.empty() ? "Request not found" : error));
  }
  if (ppt_request.output_path.empty()) {
    return HttpResponse::Json(404, ErrorJson("ERR_PPT_FILE_NOT_GENERATED", "PPT file not generated"));
  }

  // 读取结构 JSON：优先 body，其次结构文件
  nlohmann::json structure_json;
  if (!request.body.empty() && request.body.find('\0') == std::string::npos) {
    try {
      structure_json = nlohmann::json::parse(request.body);
    } catch (...) {
      return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "Invalid JSON"));
    }
  }
  if (!structure_json.is_object()) {
    const auto structure_path = BuildStructurePath(generation_config_, ppt_request.output_path);
    if (!structure_path.empty()) {
      ReadJsonFile(structure_path, structure_json);
    }
  }
  if (!structure_json.is_object()) {
    return HttpResponse::Json(400, ErrorJson("ERR_PPT_STRUCTURE_NOT_FOUND", "Structure data not found"));
  }

  std::vector<SlideContent> slides;
  std::string convert_error;
  if (!EditableJsonToSlides(structure_json, slides, convert_error)) {
    return HttpResponse::Json(400, ErrorJson("ERR_PPT_STRUCTURE_INVALID", convert_error));
  }

  std::string generate_error;
  bool gen_ok = false;

  // 在线编辑再生成：统一走 PptxGenJS 纯样式模式，避免依赖模板复杂度
  nlohmann::json style_options;
  const std::string theme_id = structure_json.value("theme_id", ppt_request.style);
  if (!theme_id.empty()) {
    style_options["themePreset"] = theme_id;
  }
  const auto options_json = style_options.empty() ? "" : style_options.dump();
  gen_ok = RunPptxGenFromPreset(slides, ppt_request.output_path, ppt_request.style, options_json, generation_config_, generate_error);

  if (!gen_ok) {
    Logger::Warn("PPTX regenerate-from-structure failed: " + generate_error);
    return HttpResponse::Json(500, ErrorJson("ERR_PPT_REGENERATE_FAILED", "Regeneration failed"));
  }

  std::string update_error;
  if (!ppt_service_->UpdateRequestOutput(ppt_request.id, user->id, ppt_request.output_path, "completed", update_error)) {
    Logger::Warn("UpdateRequestOutput after regenerate failed: " + update_error);
  }

  // 重新生成本地预览 JSON，便于前端使用 /preview
  nlohmann::json preview_payload;
  preview_payload["slides"] = nlohmann::json::array();
  for (const auto& s : slides) {
    preview_payload["slides"].push_back(SlideToJson(s));
  }
  std::filesystem::path preview_path(ppt_request.output_path);
  preview_path.replace_extension(".json");
  WriteJsonFile(preview_path, preview_payload);

  std::string signed_url;
  std::string signed_url_pdf;
  if (s3_client_ && s3_client_->IsEnabled()) {
    const auto object_key = BuildObjectKey(generation_config_, ppt_request.output_path);
    if (!object_key.empty()) {
      signed_url = s3_client_->PresignGetUrl(object_key);
    }
  }
  nlohmann::json payload;
  payload["downloadUrl"] = signed_url.empty() ? "/api/ppt/file?id=" + std::to_string(ppt_request.id)
                                              : signed_url;
  return HttpResponse::Json(200, payload);
}

std::shared_ptr<User> PptController::Authenticate(const HttpRequest& request, std::string& error) const {
  const auto token = ExtractToken(request);
  if (token.empty()) {
    error = "Token not provided";
    return nullptr;
  }

  auto user = auth_service_->GetUserFromToken(token, error);
  if (!user) {
    error = error.empty() ? "Invalid token" : error;
    return nullptr;
  }

  return std::make_shared<User>(*user);
}

std::uint64_t PptController::ParseId(const std::string& str) const {
  try {
    return std::stoull(str);
  } catch (...) {
    return 0;
  }
}

HttpResponse PptController::BatchDownload(const HttpRequest& request) {
  std::string error;
  auto user = Authenticate(request, error);
  if (!user) {
    return HttpResponse::Json(401, ErrorJson("ERR_UNAUTHORIZED", error.empty() ? "Unauthorized" : error));
  }

  // ── 1. 解析请求体 ──────────────────────────────────────────────────────────
  nlohmann::json body;
  try {
    body = nlohmann::json::parse(request.body);
  } catch (...) {
    return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "Invalid JSON body"));
  }

  if (!body.contains("ids") || !body["ids"].is_array() || body["ids"].empty()) {
    return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "Missing or empty 'ids' array"));
  }

  constexpr int kMaxBatch = 20;
  const auto& ids_json = body["ids"];
  if (static_cast<int>(ids_json.size()) > kMaxBatch) {
    return HttpResponse::Json(400, ErrorJson("ERR_BATCH_TOO_MANY",
        "单次最多下载 " + std::to_string(kMaxBatch) + " 个文件"));
  }

  // 解析 id 列表（支持数字或字符串形式）
  std::vector<std::uint64_t> ids;
  for (const auto& v : ids_json) {
    std::uint64_t id = 0;
    if (v.is_number_unsigned()) {
      id = v.get<std::uint64_t>();
    } else if (v.is_string()) {
      try { id = std::stoull(v.get<std::string>()); } catch (...) {}
    }
    if (id > 0) ids.push_back(id);
  }

  if (ids.empty()) {
    return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "No valid ids provided"));
  }

  // ── 2. 批量查询，仅返回属于当前用户且已完成的记录 ──────────────────────────
  std::string svc_error;
  const auto requests = ppt_service_->GetRequestsByIds(user->id, ids, svc_error);

  // 过滤有效文件（status=completed 且 output_path 非空且文件存在）
  struct FileEntry {
    std::string path;
    std::string display_name;   // 用于 ZIP 内的文件名
  };
  std::vector<FileEntry> entries;
  for (const auto& req : requests) {
    if (req.status != "completed" || req.output_path.empty()) continue;
    std::error_code ec;
    if (!std::filesystem::exists(req.output_path, ec) || ec) continue;
    // 生成人类可读文件名：id_title.pptx
    std::string display = std::to_string(req.id) + "_";
    const std::string& raw = req.title.empty() ? req.topic : req.title;
    // 保留字母/数字/中文，其余替换为下划线
    for (unsigned char c : raw) {
      if (std::isalnum(c) || c > 127) display.push_back(static_cast<char>(c));
      else display.push_back('_');
    }
    if (display.size() > 80) display.resize(80);
    display += ".pptx";
    entries.push_back({req.output_path, display});
  }

  if (entries.empty()) {
    return HttpResponse::Json(404, ErrorJson("ERR_NOT_FOUND",
        "没有找到可下载的已完成文件（请确认所选 PPT 状态为已完成且文件存在）"));
  }

  // ── 3. 创建临时目录，复制文件，zip 打包 ────────────────────────────────────
  // 用毫秒时间戳生成唯一临时路径
  const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
  const std::string tmp_dir  = "/tmp/ppt_batch_" + std::to_string(now_ms);
  const std::string zip_path = tmp_dir + ".zip";

  {
    std::error_code ec;
    std::filesystem::create_directory(tmp_dir, ec);
    if (ec) {
      Logger::Error("BatchDownload: cannot create tmp dir: " + tmp_dir + " err=" + ec.message());
      return HttpResponse::Json(500, ErrorJson("ERR_INTERNAL", kInternalErrorMessage));
    }
  }

  // 处理同名文件冲突：加序号前缀
  std::unordered_map<std::string, int> name_count;
  for (auto& e : entries) {
    auto& cnt = name_count[e.display_name];
    if (cnt > 0) {
      // 在扩展名前插入序号
      const auto dot = e.display_name.rfind('.');
      if (dot != std::string::npos) {
        e.display_name = e.display_name.substr(0, dot) + "_" + std::to_string(cnt) + e.display_name.substr(dot);
      } else {
        e.display_name += "_" + std::to_string(cnt);
      }
    }
    ++cnt;
  }

  // 复制文件到临时目录
  bool any_copy_ok = false;
  for (const auto& e : entries) {
    std::error_code ec;
    const std::string dest = tmp_dir + "/" + e.display_name;
    std::filesystem::copy_file(e.path, dest, std::filesystem::copy_options::overwrite_existing, ec);
    if (!ec) {
      any_copy_ok = true;
    } else {
      Logger::Warn("BatchDownload: copy failed: " + e.path + " -> " + dest + " err=" + ec.message());
    }
  }

  if (!any_copy_ok) {
    std::filesystem::remove_all(tmp_dir);
    return HttpResponse::Json(500, ErrorJson("ERR_INTERNAL", "文件复制失败"));
  }

  // 调用系统 zip 命令打包（-j 表示不保留目录结构，-q 安静模式）
  const std::string zip_cmd = "zip -j -q \"" + zip_path + "\" \"" + tmp_dir + "\"/*.pptx 2>/dev/null";
  const int zip_ret = std::system(zip_cmd.c_str());

  // 清理临时目录
  {
    std::error_code ec;
    std::filesystem::remove_all(tmp_dir, ec);
  }

  if (zip_ret != 0) {
    std::error_code ec;
    std::filesystem::remove(zip_path, ec);
    Logger::Error("BatchDownload: zip failed, exit=" + std::to_string(zip_ret));
    return HttpResponse::Json(500, ErrorJson("ERR_INTERNAL", "ZIP 打包失败"));
  }

  std::error_code size_ec;
  const auto zip_size = std::filesystem::file_size(zip_path, size_ec);
  if (size_ec || zip_size == 0) {
    std::filesystem::remove(zip_path);
    return HttpResponse::Json(500, ErrorJson("ERR_INTERNAL", "ZIP 文件生成异常"));
  }

  // ── 4. 将 ZIP 移动到持久目录，返回下载 URL（避免将大文件读入内存后通过代理传输）─
  const std::string batch_dir = generation_config_.output_dir + "/batch_zips";
  {
    std::error_code ec;
    std::filesystem::create_directories(batch_dir, ec);
  }

  // 使用时间戳作为 token，避免冲突
  const std::string token = std::to_string(now_ms);

  // 构造下载文件名：ppt_batch_YYYYMMDD.zip
  std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  char date_buf[16];
  std::strftime(date_buf, sizeof(date_buf), "%Y%m%d", std::localtime(&t));
  const std::string zip_filename = std::string("ppt_batch_") + date_buf + ".zip";

  const std::string dest_zip = batch_dir + "/" + token + ".zip";
  {
    std::error_code ec;
    std::filesystem::rename(zip_path, dest_zip, ec);
    if (ec) {
      // rename 跨设备时 fallback 到 copy
      std::filesystem::copy_file(zip_path, dest_zip, std::filesystem::copy_options::overwrite_existing, ec);
      std::filesystem::remove(zip_path);
      if (ec) {
        Logger::Error("BatchDownload: move zip failed: " + ec.message());
        return HttpResponse::Json(500, ErrorJson("ERR_INTERNAL", "ZIP 保存失败"));
      }
    }
  }

  // 返回 JSON，前端凭 token 通过 GET /api/ppt/batch_zip 直接下载
  nlohmann::json result;
  result["download_url"] = "/api/ppt/batch_zip?token=" + token;
  result["filename"]     = zip_filename;
  result["size"]         = zip_size;
  return HttpResponse::Json(200, result);
}

HttpResponse PptController::BatchDownloadFile(const HttpRequest& request) {
  // 验证用户身份（支持 Authorization header 或 ?auth= query param）
  std::string auth_error;
  auto user = Authenticate(request, auth_error);
  if (!user) {
    // 尝试从 query param auth= 读取 token
    if (auto it = request.query_params.find("auth"); it != request.query_params.end()) {
      HttpRequest fake_req = request;
      fake_req.headers["authorization"] = "Bearer " + it->second;
      user = Authenticate(fake_req, auth_error);
    }
    if (!user) {
      return HttpResponse::Json(401, ErrorJson("ERR_UNAUTHORIZED", "Unauthorized"));
    }
  }

  // 验证 token 格式为纯数字（防路径穿越）
  std::string token;
  if (auto it = request.query_params.find("token"); it != request.query_params.end()) {
    token = it->second;
  }
  if (token.empty() || token.find_first_not_of("0123456789") != std::string::npos) {
    return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "Invalid token"));
  }

  const std::string zip_path = generation_config_.output_dir + "/batch_zips/" + token + ".zip";

  std::error_code size_ec;
  const auto file_size = std::filesystem::file_size(zip_path, size_ec);
  if (size_ec || file_size == 0) {
    return HttpResponse::Json(404, ErrorJson("ERR_NOT_FOUND", "ZIP 文件不存在或已过期"));
  }

  // 使用 sendfile 系统调用流式传输，零拷贝，不受 HTTP server 缓冲区限制
  int fd = ::open(zip_path.c_str(), O_RDONLY);
  if (fd < 0) {
    return HttpResponse::Json(500, ErrorJson("ERR_INTERNAL", kInternalErrorMessage));
  }

  // 构造下载文件名
  std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
  char date_buf[16];
  std::strftime(date_buf, sizeof(date_buf), "%Y%m%d", std::localtime(&t));
  const std::string zip_filename = std::string("ppt_batch_") + date_buf + ".zip";

  // 将文件内容读入 string（此路由由浏览器直接请求，无 Vite proxy 缓冲限制）
  std::string body;
  body.resize(file_size);
  ssize_t total = 0;
  while (total < static_cast<ssize_t>(file_size)) {
    ssize_t n = ::read(fd, body.data() + total, file_size - total);
    if (n <= 0) break;
    total += n;
  }
  ::close(fd);
  body.resize(total);

  // 异步删除（TTL：下载完成即删除）
  std::thread([zip_path]() {
    std::error_code ec;
    std::filesystem::remove(zip_path, ec);
  }).detach();

  HttpResponse resp;
  resp.status_code    = 200;
  resp.status_message = "OK";
  resp.headers["Content-Type"]        = "application/zip";
  resp.headers["Content-Disposition"] = "attachment; filename=\"" + zip_filename + "\"";
  resp.headers["Content-Length"]      = std::to_string(body.size());
  resp.body = std::move(body);
  return resp;
}
