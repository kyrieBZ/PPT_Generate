#include "controllers/template_controller.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_set>

#include "http/http_types.h"

TemplateController::TemplateController(std::shared_ptr<TemplateService>        service,
                                       std::shared_ptr<TemplateManagerService> tmpl_mgr_service,
                                       std::shared_ptr<TemplateFastDfsService> tmpl_fastdfs_service)
    : service_(std::move(service)),
      tmpl_mgr_service_(std::move(tmpl_mgr_service)),
      tmpl_fastdfs_service_(std::move(tmpl_fastdfs_service)) {}

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

  // 填充 FastDFS URL（若服务可用）
  if (tmpl_fastdfs_service_) {
    for (auto& r : results) {
      auto entry = tmpl_fastdfs_service_->GetEntry(r.id);
      if (entry) {
        r.fastdfs_pptx_url       = entry->pptx_url;
        r.fastdfs_thumbnail_url  = entry->thumbnail_url;
      }
    }
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
  // 只要模板可下载（本地文件存在 或 FastDFS 已有记录），就暴露下载端点
  const bool can_download = item.has_local_file || !item.fastdfs_pptx_url.empty();
  if (can_download) {
    json_item["hasLocalFile"]     = true;
    json_item["localDownloadUrl"] = "/api/templates/file?id=" + item.id;
  }
  if (item.preview_image.empty() && item.fastdfs_thumbnail_url.empty()) {
    json_item["previewImage"] = "/api/templates/preview?id=" + item.id;
  }
  // FastDFS 访问 URL（若已迁移，同时覆盖预览图）
  if (!item.fastdfs_pptx_url.empty()) {
    json_item["fastdfsDownloadUrl"] = item.fastdfs_pptx_url;
  }
  if (!item.fastdfs_thumbnail_url.empty()) {
    json_item["previewImage"] = item.fastdfs_thumbnail_url;
  }
  return json_item;
}

HttpResponse TemplateController::Preview(const HttpRequest& request) {
  const auto it = request.query_params.find("id");
  if (it == request.query_params.end() || it->second.empty()) {
    return HttpResponse::Json(400, ErrorJson("ERR_TEMPLATE_ID_MISSING", "Template ID missing"));
  }
  const std::string& template_id = it->second;

  // 优先使用 FastDFS 缩略图 URL（302 重定向）
  if (tmpl_fastdfs_service_) {
    auto entry = tmpl_fastdfs_service_->GetEntry(template_id);
    if (entry && !entry->thumbnail_url.empty()) {
      HttpResponse resp;
      resp.status_code = 302;
      resp.status_message = "Found";
      resp.headers["location"] = entry->thumbnail_url;
      resp.headers["cache-control"] = "public, max-age=86400";
      return resp;
    }
  }

  auto path = service_->GetPreviewPath(template_id);
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

  // 优先使用 FastDFS pptx URL（302 重定向）
  if (tmpl_fastdfs_service_) {
    auto entry = tmpl_fastdfs_service_->GetEntry(template_info->id);
    if (entry && !entry->pptx_url.empty()) {
      HttpResponse resp;
      resp.status_code = 302;
      resp.status_message = "Found";
      resp.headers["location"] = entry->pptx_url;
      resp.headers["content-disposition"] = "attachment; filename=\"" + template_info->id + ".pptx\"";
      return resp;
    }
  }

  // 降级：读取本地文件
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
