#include "controllers/ppt_controller.h"

#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <sstream>

#include "logger.h"

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

nlohmann::json RequestToJson(const PptRequest& request) {
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
  if (has_file) {
    result["downloadUrl"] = "/api/ppt/file?id=" + std::to_string(request.id);
  }
  return result;
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
                           std::shared_ptr<QwenClient> qwen_client)
    : auth_service_(std::move(auth_service)),
      ppt_service_(std::move(ppt_service)),
      model_service_(std::move(model_service)),
      template_service_(std::move(template_service)),
      generation_config_(std::move(generation_config)),
      qwen_client_(std::move(qwen_client)) {}

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
      std::vector<SlideContent> slides;
      std::string qwen_error;
      if (qwen_client_->GenerateSlides(input.topic, input.pages, template_prompt, input.include_images, slides, qwen_error)) {
        payload["preview"] = nlohmann::json::array();
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
            payload["request"] = RequestToJson(ppt_request);
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
    payload["items"].push_back(RequestToJson(item));
  }

  return HttpResponse::Json(200, payload);
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
