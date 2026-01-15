#include "controllers/ppt_controller.h"

#include "logger.h"

namespace {
nlohmann::json RequestToJson(const PptRequest& request) {
  return {
      {"id", request.id},
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
      {"createdAt", request.created_at},
      {"updatedAt", request.updated_at}};
}

std::uint64_t ParseId(const std::string& value) {
  if (value.empty()) {
    return 0;
  }
  try {
    return std::stoull(value);
  } catch (const std::exception&) {
    return 0;
  }
}

std::string JoinTags(const std::vector<std::string>& tags) {
  if (tags.empty()) {
    return {};
  }
  std::string aggregated;
  for (size_t i = 0; i < tags.size(); ++i) {
    if (i > 0) {
      aggregated += "、";
    }
    aggregated += tags[i];
  }
  return aggregated;
}

std::string BuildTemplatePrompt(const RemoteTemplate& tpl, const std::string& expected_style) {
  std::string prompt = tpl.name + " 模板（来源：" + tpl.provider + "）。" + tpl.description;
  const auto tags = JoinTags(tpl.tags);
  if (!tags.empty()) {
    prompt += " 关键词：" + tags + "。";
  }
  if (!expected_style.empty()) {
    prompt += " 用户期望风格：" + expected_style + "。";
  }
  prompt += " 需要保持配色与布局与模板预览一致。 主色：" + tpl.theme.primary_color +
            "，辅色：" + tpl.theme.secondary_color + "，强调色：" + tpl.theme.accent_color + "。";
  if (!tpl.layouts.empty()) {
    prompt += " 可用版式：";
    for (size_t i = 0; i < tpl.layouts.size(); ++i) {
      if (i > 0) prompt += "；";
      prompt += tpl.layouts[i].name + "(" + tpl.layouts[i].type + ")";
    }
    prompt += "。";
  }
  return prompt;
}
}

PptController::PptController(std::shared_ptr<AuthService> auth_service,
                             std::shared_ptr<PptService> ppt_service,
                             std::shared_ptr<ModelService> model_service,
                             std::shared_ptr<TemplateService> template_service,
                             std::shared_ptr<QwenClient> qwen_client)
    : auth_service_(std::move(auth_service)),
      ppt_service_(std::move(ppt_service)),
      model_service_(std::move(model_service)),
      template_service_(std::move(template_service)),
      qwen_client_(std::move(qwen_client)) {}

HttpResponse PptController::Generate(const HttpRequest& request) {
  std::string error;
  auto user = Authenticate(request, error);
  if (!user) {
    return HttpResponse::Json(401, {{"message", error.empty() ? "未授权" : error}});
  }

  try {
    auto body = nlohmann::json::parse(request.body);
    if (!body.contains("title") || !body.contains("topic")) {
      return HttpResponse::Json(400, {{"message", "请填写标题和主题描述"}});
    }

    PptRequestInput input;
    input.title = body.value("title", "");
    input.topic = body.value("topic", "");
    input.pages = body.value("pages", 10);
    input.style = body.value("style", "business");
    input.include_images = body.value("includeImages", true);
    input.include_charts = body.value("includeCharts", true);
    input.include_notes = body.value("includeNotes", false);
    input.model_id = body.value("modelId", "qwen-turbo");
    input.template_id = body.value("templateId", "");
    auto model = model_service_->FindById(input.model_id);
    if (!model) {
      return HttpResponse::Json(400, {{"message", "模型不存在"}});
    }

    if (input.template_id.empty()) {
      return HttpResponse::Json(400, {{"message", "请选择模板"}});
    }
    if (!template_service_) {
      return HttpResponse::Json(500, {{"message", "模板服务未启用"}});  // shouldn't happen
    }
    auto template_info_opt = template_service_->FindById(input.template_id);
    if (!template_info_opt) {
      return HttpResponse::Json(400, {{"message", "模板不存在"}});
    }
    const auto template_prompt = BuildTemplatePrompt(*template_info_opt, input.style);

    if (input.pages < 1) {
      input.pages = 1;
    } else if (input.pages > 50) {
      input.pages = 50;
    }

    if (input.title.empty() || input.topic.empty()) {
      return HttpResponse::Json(400, {{"message", "标题和主题不能为空"}});
    }

    PptRequest ppt_request;
    if (!ppt_service_->CreateRequest(input, user->id, model->name, template_info_opt->name, ppt_request, error)) {
      return HttpResponse::Json(500, {{"message", error.empty() ? "生成失败" : error}});
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
      } else {
        Logger::Warn("通义千问生成失败: " + qwen_error);
      }
    }
    return HttpResponse::Json(201, payload);
  } catch (const std::exception& ex) {
    Logger::Error(std::string("解析PPT请求失败: ") + ex.what());
    return HttpResponse::Json(400, {{"message", "无效的JSON"}});
  }
}

HttpResponse PptController::History(const HttpRequest& request) {
  std::string error;
  auto user = Authenticate(request, error);
  if (!user) {
    return HttpResponse::Json(401, {{"message", error.empty() ? "未授权" : error}});
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
    return HttpResponse::Json(401, {{"message", error.empty() ? "未授权" : error}});
  }

  std::uint64_t request_id = 0;
  if (auto it = request.query_params.find("id"); it != request.query_params.end()) {
    request_id = ParseId(it->second);
  }

  if (!request_id && !request.body.empty()) {
    try {
      auto body = nlohmann::json::parse(request.body);
      request_id = body.value("id", 0ULL);
    } catch (const std::exception&) {
      // ignore json parse errors here, will fall through to invalid parameter response
    }
  }

  if (!request_id) {
    return HttpResponse::Json(400, {{"message", "缺少记录ID"}});
  }

  std::string delete_error;
  if (!ppt_service_->DeleteRequest(user->id, request_id, delete_error)) {
    if (delete_error == "记录不存在或无权删除") {
      return HttpResponse::Json(404, {{"message", delete_error}});
    }
    if (delete_error.empty()) {
      delete_error = "删除失败";
    }
    return HttpResponse::Json(500, {{"message", delete_error}});
  }

  return HttpResponse::Json(200, {{"message", "删除成功"}});
}

std::optional<User> PptController::Authenticate(const HttpRequest& request, std::string& error_message) const {
  auto header = request.Header("authorization");
  if (header.rfind("Bearer ", 0) == 0 || header.rfind("bearer ", 0) == 0) {
    header = header.substr(7);
  }
  if (header.empty()) {
    error_message = "未提供Token";
    return std::nullopt;
  }
  return auth_service_->GetUserFromToken(header, error_message);
}

nlohmann::json PptController::SlideToJson(const SlideContent& slide,
                                          const TemplateLayout* layout,
                                          const TemplateTheme* theme) const {
  nlohmann::json payload = {
      {"title", slide.title},
      {"bullets", slide.bullets},
      {"rawText", slide.raw_text},
      {"imagePrompts", slide.image_prompts},
      {"imageUrls", slide.image_urls}};
  if (layout) {
    payload["layout"] = {
        {"id", layout->id},
        {"name", layout->name},
        {"type", layout->type},
        {"description", layout->description},
        {"accentColor", layout->accent_color},
        {"backgroundImage", layout->background_image}};
  }
  if (theme) {
    payload["theme"] = {
        {"primaryColor", theme->primary_color},
        {"secondaryColor", theme->secondary_color},
        {"accentColor", theme->accent_color},
        {"backgroundImage", theme->background_image}};
  }
  return payload;
}
