#include "controllers/ppt_controller.h"

#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <sstream>
#include <vector>

#include "logger.h"
#include "models/outline_item.h"

namespace {

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
                           std::shared_ptr<S3Client> s3_client)
    : auth_service_(std::move(auth_service)),
      ppt_service_(std::move(ppt_service)),
      model_service_(std::move(model_service)),
      template_service_(std::move(template_service)),
      generation_config_(std::move(generation_config)),
      qwen_client_(std::move(qwen_client)),
      s3_client_(std::move(s3_client)) {}

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

      if (!outline.empty()) {
        if (qwen_client_->GenerateSlidesFromOutline(input.topic, outline, input.include_images, slides, qwen_error)) {
          generated = true;
        } else {
          Logger::Warn("PPT content generation from outline failed: " + qwen_error);
          slides = BuildSlidesFromOutline(outline, input.topic, input.include_images);
          generated = !slides.empty();
        }
      }

      if (!generated) {
        if (qwen_client_->GenerateSlides(input.topic, input.pages, template_prompt, input.include_images, slides, qwen_error)) {
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

        const auto template_file = template_service_->GetLocalFile(template_info_opt->id);
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
