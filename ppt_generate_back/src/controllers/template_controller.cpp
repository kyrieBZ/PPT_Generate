#include "controllers/template_controller.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_set>

#include "http/http_types.h"

TemplateController::TemplateController(std::shared_ptr<TemplateService>        service,
                                       std::shared_ptr<TemplateManagerService> tmpl_mgr_service)
    : service_(std::move(service)),
      tmpl_mgr_service_(std::move(tmpl_mgr_service)) {}

HttpResponse TemplateController::List(const HttpRequest& request) {
  const auto it = request.query_params.find("q");
  const auto query = (it != request.query_params.end()) ? it->second : std::string();

  auto results = service_->Search(query);

  // 若模板管理服务可用，则只返回已上架且在有效期内的模板
  if (tmpl_mgr_service_) {
    std::vector<std::string> active_ids;
    std::string error;
    if (tmpl_mgr_service_->ListActiveIds(active_ids, error)) {
      std::unordered_set<std::string> active_set(active_ids.begin(), active_ids.end());
      std::vector<RemoteTemplate> filtered;
      filtered.reserve(results.size());
      for (auto& r : results) {
        if (active_set.count(r.id)) {
          filtered.push_back(std::move(r));
        }
      }
      results = std::move(filtered);
    }
    // 若查询失败（数据库问题），降级为返回全部模板，不影响可用性
  }

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
  if (item.preview_image.empty()) {
    json_item["previewImage"] = "/api/templates/preview?id=" + item.id;
  }
  return json_item;
}

HttpResponse TemplateController::Preview(const HttpRequest& request) {
  const auto it = request.query_params.find("id");
  if (it == request.query_params.end() || it->second.empty()) {
    return HttpResponse::Json(400, ErrorJson("ERR_TEMPLATE_ID_MISSING", "Template ID missing"));
  }
  auto path = service_->GetPreviewPath(it->second);
  if (!path) {
    return HttpResponse::Json(404, ErrorJson("ERR_PREVIEW_NOT_FOUND", "Preview image not found"));
  }
  std::ifstream input(*path, std::ios::binary);
  if (!input.is_open()) {
    return HttpResponse::Json(500, ErrorJson("ERR_INTERNAL", kInternalErrorMessage));
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  std::string ext = std::filesystem::path(*path).extension().string();
  std::string content_type = (ext == ".png") ? "image/png" : (ext == ".jpg" || ext == ".jpeg") ? "image/jpeg" : "application/octet-stream";
  HttpResponse response;
  response.status_code = 200;
  response.status_message = "OK";
  response.headers["content-type"] = content_type;
  response.headers["cache-control"] = "public, max-age=86400";
  response.body = buffer.str();
  return response;
}

HttpResponse TemplateController::Download(const HttpRequest& request) {
  const auto it = request.query_params.find("id");
  if (it == request.query_params.end() || it->second.empty()) {
    return HttpResponse::Json(400, ErrorJson("ERR_TEMPLATE_ID_MISSING", "Template ID missing"));
  }
  const auto template_info = service_->FindById(it->second);
  if (!template_info) {
    return HttpResponse::Json(404, ErrorJson("ERR_TEMPLATE_NOT_FOUND", "Template file does not exist"));
  }
  auto local_file = service_->GetLocalFile(template_info->id);
  if (!local_file) {
    return HttpResponse::Json(404, ErrorJson("ERR_TEMPLATE_FILE_MISSING", "Template file is missing"));
  }
  std::ifstream input(*local_file, std::ios::binary);
  if (!input.is_open()) {
    return HttpResponse::Json(500, ErrorJson("ERR_INTERNAL", kInternalErrorMessage));
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
