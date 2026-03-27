#include "controllers/template_controller.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_set>

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include "http/http_types.h"
#include "logger.h"

TemplateController::TemplateController(std::shared_ptr<TemplateService>          service,
                                       std::shared_ptr<TemplateManagerService>   tmpl_mgr_service,
                                       std::shared_ptr<TemplateFastDfsService>   tmpl_fastdfs_service,
                                       std::shared_ptr<TemplateRecommendService> tmpl_recommend_service)
    : service_(std::move(service)),
      tmpl_mgr_service_(std::move(tmpl_mgr_service)),
      tmpl_fastdfs_service_(std::move(tmpl_fastdfs_service)),
      tmpl_recommend_service_(std::move(tmpl_recommend_service)) {}

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
  // 始终通过后端 proxy 接口暴露缩略图，避免将内网 FastDFS 地址直接下发给前端
  json_item["previewImage"] = "/api/templates/preview?id=" + item.id;
  if (!item.fastdfs_pptx_url.empty()) {
    json_item["fastdfsDownloadUrl"] = item.fastdfs_pptx_url;
  }
  return json_item;
}

HttpResponse TemplateController::Preview(const HttpRequest& request) {
  const auto it = request.query_params.find("id");
  if (it == request.query_params.end() || it->second.empty()) {
    return HttpResponse::Json(400, ErrorJson("ERR_TEMPLATE_ID_MISSING", "Template ID missing"));
  }
  const std::string& template_id = it->second;

  // 若 FastDFS 缩略图可用，由后端 fetch 后直接作为响应体返回，
  // 避免将内网 FastDFS 地址暴露给浏览器做 302 重定向（内网地址客户端不可达）
  if (tmpl_fastdfs_service_) {
    auto entry = tmpl_fastdfs_service_->GetEntry(template_id);
    if (entry && !entry->thumbnail_url.empty()) {
      CURL* curl = curl_easy_init();
      if (curl) {
        std::string img_body;
        char content_type_buf[256] = {};
        struct curl_slist* headers = nullptr;
        auto write_cb = +[](void* ptr, std::size_t size, std::size_t nmemb, void* userp) -> std::size_t {
          static_cast<std::string*>(userp)->append(static_cast<char*>(ptr), size * nmemb);
          return size * nmemb;
        };
        curl_easy_setopt(curl, CURLOPT_URL, entry->thumbnail_url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &img_body);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 8L);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        const CURLcode rc = curl_easy_perform(curl);
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        char* ct_ptr = nullptr;
        curl_easy_getinfo(curl, CURLINFO_CONTENT_TYPE, &ct_ptr);
        if (ct_ptr) std::snprintf(content_type_buf, sizeof(content_type_buf), "%s", ct_ptr);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (rc == CURLE_OK && http_code == 200 && !img_body.empty()) {
          HttpResponse resp;
          resp.status_code = 200;
          resp.status_message = "OK";
          resp.headers["content-type"] = content_type_buf[0] ? content_type_buf : "image/png";
          resp.headers["cache-control"] = "public, max-age=86400";
          resp.body = std::move(img_body);
          return resp;
        }
        // FastDFS fetch 失败，降级到本地文件
        Logger::Warn("TemplateController::Preview: FastDFS fetch failed for " +
                     template_id + " (curl=" + std::to_string(rc) +
                     " http=" + std::to_string(http_code) + "), falling back to local");
      }
    }
  }

  // 降级：读取本地缩略图文件
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

// ──────────────────────────────────────────────────────────────────────────────
// F07：AI 智能模板推荐

HttpResponse TemplateController::Recommend(const HttpRequest& request) {
  const bool available = tmpl_recommend_service_ && tmpl_recommend_service_->IsAvailable();

  // 解析请求体
  std::string topic;
  int top_k = 5;
  try {
    const auto body = nlohmann::json::parse(request.body);
    topic = body.value("topic", "");
    top_k = body.value("top_k", 5);
    top_k = std::clamp(top_k, 1, 10);
  } catch (...) {
    return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "请求体 JSON 格式错误"));
  }

  if (topic.empty()) {
    return HttpResponse::Json(400, ErrorJson("ERR_MISSING_PARAM", "缺少 topic 参数"));
  }

  // 构建上架模板 ID 集合，用于后续过滤向量检索结果和降级推荐
  // 若 tmpl_mgr_service_ 不可用或查询失败，active_set 为空，则不过滤（全库降级）
  std::unordered_set<std::string> active_set;
  std::vector<RemoteTemplate> active_templates;
  bool has_active_filter = false;

  if (tmpl_mgr_service_) {
    std::vector<std::string> active_ids;
    std::string mgr_err;
    if (tmpl_mgr_service_->ListActiveIds(active_ids, mgr_err) && !active_ids.empty()) {
      active_set.insert(active_ids.begin(), active_ids.end());
      has_active_filter = true;
    }
  }

  // 如果有上架过滤集，从全库筛出上架模板用于降级推荐
  if (has_active_filter) {
    const auto all = service_->GetAll();
    for (const auto& t : all) {
      if (active_set.count(t.id)) active_templates.push_back(t);
    }
  } else {
    active_templates = service_->GetAll();
  }

  // 降级：AI 服务不可用时，从上架模板中取 top_k 个
  if (!available) {
    nlohmann::json recs = nlohmann::json::array();
    const int limit = std::min(top_k, static_cast<int>(active_templates.size()));
    for (int i = 0; i < limit; ++i) {
      const auto& t = active_templates[i];
      std::string tag_str;
      for (const auto& tag : t.tags) {
        if (!tag_str.empty()) tag_str += "、";
        tag_str += tag;
      }
      recs.push_back({
        {"templateId",   t.id},
        {"name",         t.name},
        {"description",  t.description},
        {"matchReason",  "该模板风格标签（" + tag_str + "）可能适合您的主题"},
        {"score",        0.5},
        {"primaryColor", t.theme.primary_color},
        {"accentColor",  t.theme.accent_color},
        {"previewImage", "/api/templates/preview?id=" + t.id},
        {"provider",     t.provider}
      });
    }
    return HttpResponse::Json(200, {
      {"available",       false},
      {"recommendations", recs},
      {"message",         "AI 推荐服务未启用，以下为上架模板推荐（需配置 Qdrant + Qwen AI）"}
    });
  }

  // 向量检索推荐，然后用 active_set 过滤掉未上架的结果
  const auto results = tmpl_recommend_service_->Recommend(topic, top_k * 3);

  nlohmann::json recs = nlohmann::json::array();
  for (const auto& r : results) {
    // 若有上架过滤集，跳过未上架的向量检索结果
    if (has_active_filter && !active_set.count(r.template_id)) {
      continue;
    }
    recs.push_back({
      {"templateId",   r.template_id},
      {"name",         r.name},
      {"description",  r.description},
      {"matchReason",  r.match_reason},
      {"score",        r.score},
      {"primaryColor", r.primary_color},
      {"accentColor",  r.accent_color},
      {"previewImage", "/api/templates/preview?id=" + r.template_id},
      {"provider",     r.provider}
    });
    if (static_cast<int>(recs.size()) >= top_k) break;
  }

  if (recs.empty()) {
    // Qdrant 无索引或所有检索结果都被上架过滤掉了 → 降级为上架模板推荐
    const int limit = std::min(top_k, static_cast<int>(active_templates.size()));
    for (int i = 0; i < limit; ++i) {
      const auto& t = active_templates[i];
      recs.push_back({
        {"templateId",   t.id},
        {"name",         t.name},
        {"description",  t.description},
        {"matchReason",  "推荐模板（AI 索引尚未建立，请管理员在模板管理页重建索引）"},
        {"score",        0.0},
        {"primaryColor", t.theme.primary_color},
        {"accentColor",  t.theme.accent_color},
        {"previewImage", "/api/templates/preview?id=" + t.id},
        {"provider",     t.provider}
      });
    }
    return HttpResponse::Json(200, {
      {"available",       true},
      {"needsReindex",    true},
      {"recommendations", recs},
      {"message",         "模板 AI 索引尚未建立，已降级为上架模板推荐。请在管理员 → 模板管理 → 重建推荐索引。"}
    });
  }

  return HttpResponse::Json(200, {
    {"available",       true},
    {"recommendations", recs},
    {"message",         "已为您找到 " + std::to_string(recs.size()) + " 个匹配模板"}
  });
}

// ──────────────────────────────────────────────────────────────────────────────
// 管理员：全量重建模板向量索引（只针对上架中的模板）

HttpResponse TemplateController::ReindexTemplates(const HttpRequest& request) {
  (void)request;

  if (!tmpl_recommend_service_ || !tmpl_recommend_service_->IsAvailable()) {
    return HttpResponse::Json(503, ErrorJson("ERR_UNAVAILABLE",
        "模板推荐服务未启用（需配置 Qdrant + Qwen AI）"));
  }

  // 尝试获取上架模板 ID 列表；若管理员尚未上架任何模板，则索引全部模板
  auto all_templates = service_->GetAll();
  bool active_filter_applied = false;

  if (tmpl_mgr_service_) {
    std::vector<std::string> active_ids;
    std::string err;
    if (tmpl_mgr_service_->ListActiveIds(active_ids, err) && !active_ids.empty()) {
      const std::unordered_set<std::string> active_set(active_ids.begin(), active_ids.end());
      std::vector<RemoteTemplate> filtered;
      filtered.reserve(active_ids.size());
      for (auto& t : all_templates) {
        if (active_set.count(t.id)) filtered.push_back(std::move(t));
      }
      all_templates = std::move(filtered);
      active_filter_applied = true;
      Logger::Info("TemplateController::ReindexTemplates: " +
                   std::to_string(all_templates.size()) + " active templates to index");
    } else {
      // template_manager 表为空（尚未通过管理员操作上架），退化为全库索引
      Logger::Info("TemplateController::ReindexTemplates: no active records, indexing all " +
                   std::to_string(all_templates.size()) + " templates");
    }
  }

  if (all_templates.empty()) {
    return HttpResponse::Json(200, {
      {"indexed",          0},
      {"active_filter",    active_filter_applied},
      {"message",          "当前无可索引的模板，跳过索引构建"}
    });
  }

  // 转换为 TemplateRecommendService::TemplateInfo
  std::vector<TemplateRecommendService::TemplateInfo> infos;
  infos.reserve(all_templates.size());
  for (const auto& t : all_templates) {
    TemplateRecommendService::TemplateInfo info;
    info.id            = t.id;
    info.name          = t.name;
    info.description   = t.description;
    info.tags          = t.tags;
    info.primary_color = t.theme.primary_color;
    info.accent_color  = t.theme.accent_color;
    info.preview_image = t.preview_image;
    info.provider      = t.provider;
    infos.push_back(std::move(info));
  }

  std::string err;
  const int count = tmpl_recommend_service_->IndexTemplates(infos, err);
  if (count < 0) {
    return HttpResponse::Json(500, ErrorJson("ERR_INTERNAL", "索引失败: " + err));
  }

  Logger::Info("TemplateController::ReindexTemplates: indexed " +
               std::to_string(count) + " templates (active_filter=" +
               (active_filter_applied ? "true" : "false") + ")");

  const std::string scope_desc = active_filter_applied ? "上架模板" : "全部模板（建议先在模板管理中上架模板）";
  return HttpResponse::Json(200, {
    {"indexed",          count},
    {"total",            static_cast<int>(all_templates.size())},
    {"active_filter",    active_filter_applied},
    {"message",          "已为 " + std::to_string(count) + " 个" + scope_desc + "建立 AI 推荐索引"}
  });
}
