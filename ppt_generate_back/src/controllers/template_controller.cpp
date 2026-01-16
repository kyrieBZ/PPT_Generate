#include "controllers/template_controller.h"

#include <algorithm>
#include <fstream>
#include <sstream>

TemplateController::TemplateController(std::shared_ptr<TemplateService> service)
    : service_(std::move(service)) {}

HttpResponse TemplateController::List(const HttpRequest& request) {
  const auto it = request.query_params.find("q");
  const auto query = (it != request.query_params.end()) ? it->second : std::string();

  const auto results = service_->Search(query);
  nlohmann::json payload;
  payload["items"] = nlohmann::json::array();
  for (const auto& item : results) {
    payload["items"].push_back(ToJson(item));
  }
  payload["total"] = payload["items"].size();
  return HttpResponse::Json(200, payload);
}

nlohmann::json TemplateController::ToJson(const RemoteTemplate& item) {
  nlohmann::json json_item = {
      {"id", item.id},
      {"name", item.name},
      {"provider", item.provider},
      {"providerUrl", item.provider_url},
      {"description", item.description},
      {"previewImage", item.preview_image},
      {"downloadUrl", item.download_url},
      {"license", item.license},
      {"tags", item.tags},
      {"hasLocalFile", item.has_local_file}};
  json_item["theme"] = {
      {"primaryColor", item.theme.primary_color},
      {"secondaryColor", item.theme.secondary_color},
      {"accentColor", item.theme.accent_color},
      {"backgroundImage", item.theme.background_image}};
  json_item["layouts"] = nlohmann::json::array();
  for (const auto& layout : item.layouts) {
    json_item["layouts"].push_back({
        {"id", layout.id},
        {"name", layout.name},
        {"type", layout.type},
        {"description", layout.description},
        {"accentColor", layout.accent_color},
        {"backgroundImage", layout.background_image}});
  }
  if (item.has_local_file) {
    json_item["localDownloadUrl"] = "/api/templates/file?id=" + item.id;
  }
  return json_item;
}

HttpResponse TemplateController::Download(const HttpRequest& request) {
  const auto it = request.query_params.find("id");
  if (it == request.query_params.end() || it->second.empty()) {
    return HttpResponse::Json(400, {{"message", "Template ID missing"}});
  }
  const auto template_info = service_->FindById(it->second);
  if (!template_info || !template_info->has_local_file) {
    return HttpResponse::Json(404, {{"message", "Template file does not exist"}});
  }
  auto local_file = service_->GetLocalFile(template_info->id);
  if (!local_file) {
    return HttpResponse::Json(404, {{"message", "Template file is missing"}});  
  }
  std::ifstream input(*local_file, std::ios::binary);
  if (!input.is_open()) {
    return HttpResponse::Json(500, {{"message", "Cannot read template file"}});  
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();

  HttpResponse response;
  response.status_code = 200;
  response.status_message = "OK";
  response.headers["content-type"] = "application/vnd.openxmlformats-officedocument.presentationml.presentation";
  response.headers["content-disposition"] = "attachment; filename=\"" + template_info->id + ".pptx\"";
  response.body = buffer.str();
  return response;
}