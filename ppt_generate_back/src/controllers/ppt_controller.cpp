#include "controllers/ppt_controller.h"

#include <cctype>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <sstream>
#include <vector>

#include <curl/curl.h>

#include "logger.h"
#include "models/outline_item.h"

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

nlohmann::json RequestToJson(const PptRequest& request, const std::string& download_url = {}) {
  const bool has_file = !request.output_path.empty();
  nlohmann::json result = {
      {"id", request.id},
      {"userId", request.user_id},
      {"title", request.title},
      {"topic", request.topic},
      {"pages", request.pages},
      {"style", request.style},
      {"includeImages", request.include_images},
      {"includeCharts", request.include_charts},
      {"includeNotes", request.include_notes},
      {"modelId", request.model_id},
      {"modelName", request.model_name},
      {"templateId", request.template_id},
      {"templateName", request.template_name},
      {"status", request.status},
      {"createdAt", FormatTimestamp(request.created_at)},
      {"updatedAt", FormatTimestamp(request.updated_at)},
      {"hasFile", has_file}};
  if (!request.user_name.empty()) {
    result["username"] = request.user_name;
  }
  if (!request.user_email.empty()) {
    result["email"] = request.user_email;
  }
  if (has_file) {
    result["downloadUrl"] = download_url.empty()
                                ? "/api/ppt/file?id=" + std::to_string(request.id)
                                : download_url;
  }
  return result;
}

nlohmann::json OutlineItemToJson(const OutlineItem& item) {
  nlohmann::json result = {
      {"title", item.title},
      {"summary", item.summary},
      {"keyPoints", item.key_points}
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

void AttachImagesToSlides(const GenerationConfig& config,
                          DoubaoImageClient& client,
                          std::vector<SlideContent>& slides,
                          std::uint64_t request_id,
                          const std::string& topic) {
  if (!client.IsEnabled()) {
    return;
  }

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

    std::vector<DoubaoImageAsset> assets;
    std::string error;
    if (!client.GenerateImages(slide.image_prompts.front(), assets, error)) {
      Logger::Warn("Doubao image generation failed: " + error);
      continue;
    }

    const auto timeout = client.timeout_seconds();
    for (std::size_t j = 0; j < assets.size(); ++j) {
      const auto image_path = BuildImagePath(config, request_id, i, j);
      bool saved = false;
      std::string save_error;
      if (!assets[j].b64_json.empty()) {
        const auto data = Base64Decode(assets[j].b64_json);
        saved = !data.empty() && WriteBinaryFile(image_path, data);
        if (!saved) {
          save_error = "base64解码失败";
        }
      } else if (!assets[j].url.empty()) {
        saved = DownloadToFile(assets[j].url, image_path, timeout, save_error);
      }

      if (saved) {
        slide.image_paths.push_back(image_path);
        if (!assets[j].url.empty()) {
          slide.image_urls.push_back(assets[j].url);
        }
      } else {
        Logger::Warn("Image download failed: " + save_error);
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

std::string Trim(std::string value) {
  const auto start = value.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) {
    return {};
  }
  const auto end = value.find_last_not_of(" \t\r\n");
  return value.substr(start, end - start + 1);
}

std::string BuildDownloadFilename(const PptRequest& request, const User& user) {
  const auto safe_title = SanitizeFilenamePart(request.title, 80);
  const auto safe_email = SanitizeFilenamePart(user.email, 80);
  std::string filename = safe_title;
  if (!safe_email.empty()) {
    filename += "_" + safe_email;
  }
  filename += "_" + std::to_string(request.id) + ".pptx";
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

}  // namespace

PptController::PptController(std::shared_ptr<AuthService> auth_service,
                           std::shared_ptr<PptService> ppt_service,
                           std::shared_ptr<ModelService> model_service,
                           std::shared_ptr<TemplateService> template_service,
                           GenerationConfig generation_config,
                           std::shared_ptr<QwenClient> qwen_client,
                           std::shared_ptr<S3Client> s3_client,
                           std::shared_ptr<DoubaoImageClient> doubao_client)
    : auth_service_(std::move(auth_service)),
      ppt_service_(std::move(ppt_service)),
      model_service_(std::move(model_service)),
      template_service_(std::move(template_service)),
      generation_config_(std::move(generation_config)),
      qwen_client_(std::move(qwen_client)),
      s3_client_(std::move(s3_client)),
      doubao_client_(std::move(doubao_client)) {}

HttpResponse PptController::Generate(const HttpRequest& request) {
  std::string error;
  auto user = Authenticate(request, error);
  if (!user) {
    return HttpResponse::Json(401, {{"message", error.empty() ? "Unauthorized" : error}});
  }

  auto model = model_service_->FindById("qwen-turbo");
  if (!model) {
    return HttpResponse::Json(500, {{"message", "Model not found"}});
  }

  try {
    auto input = PptRequestInput::FromJson(nlohmann::json::parse(request.body));

    if (input.pages < 1) {
      input.pages = 1;
    } else if (input.pages > 50) {
      input.pages = 50;
    }

    if (input.title.empty() || input.topic.empty()) {
      return HttpResponse::Json(400, {{"message", "Title and topic cannot be empty"}});
    }

    std::string template_id;
    if (auto it = request.query_params.find("template"); it != request.query_params.end() && !it->second.empty()) {
      template_id = it->second;
    } else if (!input.template_id.empty()) {
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
      return HttpResponse::Json(400, {{"message", "Invalid template"}});
    }

    input.template_id = template_info_opt->id;

    std::string template_prompt = template_info_opt->prompt.empty()
                                      ? template_info_opt->description
                                      : template_info_opt->prompt;

    PptRequest ppt_request;
    if (!ppt_service_->CreateRequest(input, user->id, model->name, template_info_opt->name, ppt_request, error)) {
      return HttpResponse::Json(500, {{"message", error.empty() ? "Generation failed" : error}});
    }

    nlohmann::json payload{{"request", RequestToJson(ppt_request)}};
    std::optional<std::string> template_file;
    nlohmann::json template_analysis;
    bool has_template_analysis = false;
    if (qwen_client_ && qwen_client_->IsEnabled()) {
      template_file = template_service_->GetLocalFile(template_info_opt->id);
      if (template_file) {
        std::string analysis_error;
        if (EnsureTemplateAnalysis(generation_config_, template_info_opt->id, *template_file,
                                   template_analysis, analysis_error)) {
          has_template_analysis = true;
        } else {
          Logger::Warn("Template analysis failed: " + analysis_error);
        }
      }
    }
    if (input.model_id == "qwen-turbo" && qwen_client_ && qwen_client_->IsEnabled()) {
      std::vector<OutlineItem> outline = input.outline;
      if (!outline.empty() && static_cast<int>(outline.size()) > input.pages) {
        outline.resize(static_cast<std::size_t>(input.pages));
      }

      std::vector<SlideContent> slides;
      std::string qwen_error;
      bool generated = false;

      if (outline.empty()) {
        std::string outline_error;
        if (!qwen_client_->GenerateOutline(input.topic, input.pages, template_prompt, outline, outline_error)) {
          Logger::Warn("PPT outline generation failed: " + outline_error);
        }
      }

      std::string layout_guide_json;
      const int layout_slide_count = outline.empty()
                                         ? std::max(1, std::min(input.pages, 10))
                                         : static_cast<int>(outline.size());
      if (has_template_analysis && layout_slide_count > 0) {
        std::string layout_error;
        if (!LoadLayoutGuide(generation_config_, template_info_opt->id, *template_file,
                             layout_slide_count, template_prompt, template_analysis,
                             *qwen_client_, layout_guide_json, layout_error)) {
          Logger::Warn("Layout guide generation failed: " + layout_error);
          layout_guide_json.clear();
        }
      }

      if (!outline.empty()) {
        if (qwen_client_->GenerateSlidesFromOutlineWithLayout(input.topic, outline, input.include_images,
                                                              layout_guide_json, slides, qwen_error)) {
          generated = true;
        } else {
          Logger::Warn("PPT content generation from outline failed: " + qwen_error);
          slides = BuildSlidesFromOutline(outline, input.topic, input.include_images);
          generated = !slides.empty();
        }
      }

      if (!generated) {
        if (qwen_client_->GenerateSlidesWithLayout(input.topic, input.pages, template_prompt,
                                                   input.include_images, layout_guide_json,
                                                   slides, qwen_error)) {
          generated = true;
        }
      }

      if (generated) {
        payload["preview"] = nlohmann::json::array();
        if (!outline.empty()) {
          payload["outline"] = OutlineToJson(outline);
        }
        const auto& layouts = template_info_opt->layouts;
        const auto* theme = &template_info_opt->theme;
        for (size_t i = 0; i < slides.size(); ++i) {
          const TemplateLayout* layout = layouts.empty() ? nullptr : &layouts[i % layouts.size()];
          payload["preview"].push_back(SlideToJson(slides[i], layout, theme));
        }

        if (input.include_images && doubao_client_ && doubao_client_->IsEnabled()) {
          AttachImagesToSlides(generation_config_, *doubao_client_, slides, ppt_request.id, input.topic);
        }

        if (!template_file) {
          Logger::Warn("Template file missing or invalid for id: " + template_info_opt->id +
                       ", local=" + template_info_opt->local_file_path);
          std::string update_error;
          ppt_request.status = "failed";
          ppt_service_->UpdateRequestOutput(ppt_request.id, ppt_request.user_id, "", "failed", update_error);
          payload["request"] = RequestToJson(ppt_request);
          payload["fileError"] = "Template file missing or invalid";
        } else {
          std::string output_path = BuildOutputPath(generation_config_, ppt_request.id, input.title, user->email);
          Logger::Info("Generating PPT: " + output_path);
          std::string generate_error;
          if (ppt_service_->GeneratePptxFile(*template_file, slides, output_path, generate_error)) {
            std::string update_error;
            ppt_request.output_path = output_path;
            ppt_request.status = "completed";
            ppt_service_->UpdateRequestOutput(ppt_request.id, ppt_request.user_id, output_path, "completed", update_error);
            if (!outline.empty()) {
              AppendOutlineToPreviewJson(output_path, outline);
            }
            std::string signed_url;
            if (s3_client_ && s3_client_->IsEnabled()) {
              const auto object_key = BuildObjectKey(generation_config_, output_path);
              if (!object_key.empty()) {
                std::string upload_error;
                if (s3_client_->UploadFile(output_path, object_key, upload_error)) {
                  signed_url = s3_client_->PresignGetUrl(object_key);
                  Logger::Info("S3 upload success: key=" + object_key);
                } else {
                  Logger::Warn("S3 upload failed: " + upload_error);
                }
              }
            }
            payload["request"] = RequestToJson(ppt_request, signed_url);
          } else {
            Logger::Warn("PPTX generation failed: " + generate_error);
            std::string update_error;
            ppt_request.status = "failed";
            ppt_service_->UpdateRequestOutput(ppt_request.id, ppt_request.user_id, "", "failed", update_error);
            payload["request"] = RequestToJson(ppt_request);
          }
        }
      } else {
        Logger::Warn("Qwen slide generation failed: " + qwen_error);
      }
    }
    return HttpResponse::Json(201, payload);
  } catch (const std::exception& ex) {
    Logger::Error(std::string("Failed to parse PPT request: ") + ex.what());
    return HttpResponse::Json(400, {{"message", "Invalid JSON"}});
  }
}

HttpResponse PptController::History(const HttpRequest& request) {
  std::string error;
  auto user = Authenticate(request, error);
  if (!user) {
    return HttpResponse::Json(401, {{"message", error.empty() ? "Unauthorized" : error}});
  }

  std::string query;
  if (auto it = request.query_params.find("q"); it != request.query_params.end()) {
    query = Trim(it->second);
  }

  auto list = ppt_service_->GetHistory(user->id, query, error);
  if (!error.empty()) {
    return HttpResponse::Json(500, {{"message", error}});
  }

  nlohmann::json payload;
  payload["items"] = nlohmann::json::array();
  for (const auto& item : list) {
    std::string signed_url;
    if (s3_client_ && s3_client_->IsEnabled() && !item.output_path.empty()) {
      const auto object_key = BuildObjectKey(generation_config_, item.output_path);
      if (!object_key.empty()) {
        signed_url = s3_client_->PresignGetUrl(object_key);
      }
    }
    payload["items"].push_back(RequestToJson(item, signed_url));
  }

  return HttpResponse::Json(200, payload);
}

HttpResponse PptController::AdminHistory(const HttpRequest& request) {
  std::string error;
  auto user = Authenticate(request, error);
  if (!user) {
    return HttpResponse::Json(401, {{"message", error.empty() ? "Unauthorized" : error}});
  }
  if (!user->is_admin) {
    return HttpResponse::Json(403, {{"message", "Forbidden"}});
  }

  std::string query;
  if (auto it = request.query_params.find("q"); it != request.query_params.end()) {
    query = Trim(it->second);
  }

  auto list = ppt_service_->GetAdminHistory(query, error);
  if (!error.empty()) {
    return HttpResponse::Json(500, {{"message", error}});
  }

  nlohmann::json payload;
  payload["items"] = nlohmann::json::array();
  for (const auto& item : list) {
    std::string signed_url;
    if (s3_client_ && s3_client_->IsEnabled() && !item.output_path.empty()) {
      const auto object_key = BuildObjectKey(generation_config_, item.output_path);
      if (!object_key.empty()) {
        signed_url = s3_client_->PresignGetUrl(object_key);
      }
    }
    payload["items"].push_back(RequestToJson(item, signed_url));
  }

  return HttpResponse::Json(200, payload);
}

HttpResponse PptController::AdminMetrics(const HttpRequest& request) {
  std::string error;
  auto user = Authenticate(request, error);
  if (!user) {
    return HttpResponse::Json(401, {{"message", error.empty() ? "Unauthorized" : error}});
  }
  if (!user->is_admin) {
    return HttpResponse::Json(403, {{"message", "Forbidden"}});
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
    return HttpResponse::Json(500, {{"message", error.empty() ? "获取统计数据失败" : error}});
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

HttpResponse PptController::Outline(const HttpRequest& request) {
  std::string error;
  auto user = Authenticate(request, error);
  if (!user) {
    return HttpResponse::Json(401, {{"message", error.empty() ? "Unauthorized" : error}});
  }

  try {
    auto input = PptRequestInput::FromJson(nlohmann::json::parse(request.body));
    if (input.pages < 1) {
      input.pages = 1;
    } else if (input.pages > 50) {
      input.pages = 50;
    }
    if (input.title.empty() || input.topic.empty()) {
      return HttpResponse::Json(400, {{"message", "Title and topic cannot be empty"}});
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
      return HttpResponse::Json(400, {{"message", "Invalid template"}});
    }
    std::string template_prompt = template_info_opt->prompt.empty()
                                      ? template_info_opt->description
                                      : template_info_opt->prompt;

    if (!qwen_client_ || !qwen_client_->IsEnabled()) {
      return HttpResponse::Json(500, {{"message", "Model not available"}});
    }

    std::vector<OutlineItem> outline;
    std::string outline_error;
    if (!qwen_client_->GenerateOutline(input.topic, input.pages, template_prompt, outline, outline_error)) {
      return HttpResponse::Json(500, {{"message", outline_error.empty() ? "Outline generation failed" : outline_error}});
    }

    nlohmann::json payload;
    payload["outline"] = OutlineToJson(outline);
    payload["count"] = outline.size();
    return HttpResponse::Json(200, payload);
  } catch (const std::exception& ex) {
    Logger::Error(std::string("Failed to parse outline request: ") + ex.what());
    return HttpResponse::Json(400, {{"message", "Invalid JSON"}});
  }
}

HttpResponse PptController::Delete(const HttpRequest& request) {
  std::string error;
  auto user = Authenticate(request, error);
  if (!user) {
    return HttpResponse::Json(401, {{"message", error.empty() ? "Unauthorized" : error}});
  }

  std::uint64_t request_id = 0;
  if (auto it = request.query_params.find("id"); it != request.query_params.end()) {
    request_id = ParseId(it->second);
  }
  if (request_id == 0 && !request.body.empty()) {
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
    return HttpResponse::Json(400, {{"message", "Invalid request ID"}});
  }

  PptRequest ppt_request;
  if (!ppt_service_->GetRequest(user->id, request_id, ppt_request, error)) {
    return HttpResponse::Json(404, {{"message", error.empty() ? "Request not found" : error}});
  }

  if (s3_client_ && s3_client_->IsEnabled() && !ppt_request.output_path.empty()) {
    const auto object_key = BuildObjectKey(generation_config_, ppt_request.output_path);
    if (!object_key.empty()) {
      std::string delete_error;
      if (!s3_client_->DeleteObject(object_key, delete_error)) {
        Logger::Warn("S3 delete failed: key=" + object_key + " error=" + delete_error);
        return HttpResponse::Json(500, {{"message", "Failed to delete remote file"}});
      }
      Logger::Info("S3 delete success: key=" + object_key);
    }
  }

  if (!ppt_service_->DeleteRequest(user->id, request_id, error)) {
    if (error == "记录不存在或已删除") {
      return HttpResponse::Json(404, {{"message", error}});
    }
    return HttpResponse::Json(400, {{"message", error.empty() ? "Deletion failed" : error}});
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
    }
  }

  return HttpResponse::Json(200, {{"message", "Request deleted successfully"}});
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
    return HttpResponse::Json(401, {{"message", error.empty() ? "Unauthorized" : error}});
  }
  const bool is_head = request.method == "HEAD" || request.method == "head";

  std::uint64_t request_id = 0;
  if (auto it = request.query_params.find("id"); it != request.query_params.end()) {
    request_id = ParseId(it->second);
  }
  if (request_id == 0) {
    return HttpResponse::Json(400, {{"message", "Invalid request ID"}});
  }

  PptRequest ppt_request;
  if (!ppt_service_->GetRequest(user->id, request_id, ppt_request, error)) {
    Logger::Warn("PPT download request not found: user_id=" + std::to_string(user->id) +
                 " request_id=" + std::to_string(request_id));
    return HttpResponse::Json(404, {{"message", error.empty() ? "Request not found" : error}});
  }
  if (ppt_request.output_path.empty()) {
    Logger::Warn("PPT download missing output_path: user_id=" + std::to_string(user->id) +
                 " request_id=" + std::to_string(request_id));
    return HttpResponse::Json(404, {{"message", "PPT file not generated"}});
  }

  std::error_code size_ec;
  const auto file_size = std::filesystem::file_size(ppt_request.output_path, size_ec);
  if (size_ec || file_size == 0) {
    Logger::Warn("PPT download file missing: path=" + ppt_request.output_path +
                 " error=" + size_ec.message());
    return HttpResponse::Json(404, {{"message", "PPT file is missing"}});
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
    return HttpResponse::Json(404, {{"message", "PPT file is missing"}});
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
    return HttpResponse::Json(401, {{"message", error.empty() ? "Unauthorized" : error}});
  }

  std::uint64_t request_id = 0;
  if (auto it = request.query_params.find("id"); it != request.query_params.end()) {
    request_id = ParseId(it->second);
  }
  if (request_id == 0) {
    return HttpResponse::Json(400, {{"message", "Invalid request ID"}});
  }

  PptRequest ppt_request;
  if (!ppt_service_->GetRequest(user->id, request_id, ppt_request, error)) {
    return HttpResponse::Json(404, {{"message", error.empty() ? "Request not found" : error}});
  }
  if (ppt_request.output_path.empty()) {
    return HttpResponse::Json(404, {{"message", "PPT file not generated"}});
  }

  std::filesystem::path output_path(ppt_request.output_path);
  std::filesystem::path preview_path = output_path;
  preview_path.replace_extension(".json");

  const std::filesystem::path base_dir(generation_config_.output_dir);
  if (!IsUnderDirectory(base_dir, preview_path)) {
    Logger::Warn("Refusing to read preview outside generated directory: " + preview_path.string());
    return HttpResponse::Json(403, {{"message", "Preview not accessible"}});
  }

  std::ifstream input(preview_path);
  if (!input.is_open()) {
    return HttpResponse::Json(404, {{"message", "Preview data not found"}});
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
