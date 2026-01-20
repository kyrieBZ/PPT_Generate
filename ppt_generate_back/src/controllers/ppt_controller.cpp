#include "controllers/ppt_controller.h"

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
  return header;
}

std::string BuildOutputPath(const GenerationConfig& config, std::uint64_t request_id) {
  std::filesystem::path output_dir(config.output_dir);
  std::error_code ec;
  std::filesystem::create_directories(output_dir, ec);
  std::filesystem::path filename = "ppt_" + std::to_string(request_id) + ".pptx";
  return (output_dir / filename).lexically_normal().string();
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
          std::string output_path = BuildOutputPath(generation_config_, ppt_request.id);
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

  auto list = ppt_service_->GetHistory(user->id, error);
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

  if (request_id == 0) {
    return HttpResponse::Json(400, {{"message", "Invalid request ID"}});
  }

  if (!ppt_service_->DeleteRequest(user->id, request_id, error)) {
    return HttpResponse::Json(400, {{"message", error.empty() ? "Deletion failed" : error}});
  }

  return HttpResponse::Json(200, {{"message", "Request deleted successfully"}});
}

HttpResponse PptController::Download(const HttpRequest& request) {
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

  std::ifstream input(ppt_request.output_path, std::ios::binary);
  if (!input.is_open()) {
    return HttpResponse::Json(404, {{"message", "PPT file is missing"}});
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();

  HttpResponse response;
  response.status_code = 200;
  response.status_message = "OK";
  response.headers["content-type"] = "application/vnd.openxmlformats-officedocument.presentationml.presentation";
  response.headers["content-disposition"] =
      "attachment; filename=\"ppt_" + std::to_string(ppt_request.id) + ".pptx\"";
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
