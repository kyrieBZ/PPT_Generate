#include "controllers/template_manager_controller.h"

#include <filesystem>
#include <unordered_map>

#include <nlohmann/json.hpp>

#include "http/http_types.h"
#include "logger.h"
#include "utils/string_utils.h"
#include "services/template_service.h"

namespace {

std::string ExtractTokenLocal(const HttpRequest& request) {
  auto header = request.Header("authorization");
  if (header.rfind("Bearer ", 0) == 0 || header.rfind("bearer ", 0) == 0) {
    return header.substr(7);
  }
  if (!header.empty()) return header;
  if (auto it = request.query_params.find("token"); it != request.query_params.end()) {
    return it->second;
  }
  return {};
}

nlohmann::json EntryToJson(const TemplateEntry& e) {
  nlohmann::json j = {
      {"id",           e.id},
      {"templateId",   e.template_id},
      {"templateName", e.template_name},
      {"isActive",     e.is_active},
      {"availableFrom", e.available_from},
      {"createdBy",    e.created_by},
      {"createdAt",    e.created_at},
      {"updatedAt",    e.updated_at}};
  if (e.available_to.empty()) {
    j["availableTo"] = nullptr;
  } else {
    j["availableTo"] = e.available_to;
  }
  return j;
}

}  // namespace

TemplateManagerController::TemplateManagerController(
    std::shared_ptr<AuthService>            auth_service,
    std::shared_ptr<AuditService>           audit_service,
    std::shared_ptr<TemplateManagerService> tmpl_mgr_service,
    std::shared_ptr<TemplateService>        template_service,
    std::shared_ptr<TemplateFastDfsService> tmpl_fastdfs_service)
    : auth_service_(std::move(auth_service)),
      audit_service_(std::move(audit_service)),
      tmpl_mgr_service_(std::move(tmpl_mgr_service)),
      template_service_(std::move(template_service)),
      tmpl_fastdfs_service_(std::move(tmpl_fastdfs_service)) {}

std::string TemplateManagerController::ExtractIp(const HttpRequest& request) {
  auto xff = request.Header("x-forwarded-for");
  if (!xff.empty()) {
    auto pos = xff.find(',');
    return string_utils::Trim(pos != std::string::npos ? xff.substr(0, pos) : xff);
  }
  auto real = request.Header("x-real-ip");
  if (!real.empty()) return string_utils::Trim(real);
  return "unknown";
}

std::shared_ptr<User> TemplateManagerController::AuthenticateAdmin(
    const HttpRequest& request, std::string& error) const {
  const auto token = ExtractTokenLocal(request);
  if (token.empty()) { error = "Token not provided"; return nullptr; }
  auto user = auth_service_->GetUserFromToken(token, error);
  if (!user) { error = error.empty() ? "Invalid token" : error; return nullptr; }
  if (!user->is_admin) { error = "Forbidden"; return nullptr; }
  return std::make_shared<User>(*user);
}

HttpResponse TemplateManagerController::AdminList(const HttpRequest& request) {
  std::string error;
  auto admin = AuthenticateAdmin(request, error);
  if (!admin) {
    int code = (error == "Forbidden") ? 403 : 401;
    return HttpResponse::Json(code, ErrorJson("ERR_UNAUTHORIZED", error));
  }

  // 获取所有 catalog 模板
  const auto& all_catalog = template_service_->GetAll();

  // 获取数据库中的上架记录（以 template_id 为 key）
  std::vector<TemplateEntry> entries;
  std::string db_error;
  tmpl_mgr_service_->ListAll(entries, db_error);

  std::unordered_map<std::string, TemplateEntry> entry_map;
  for (auto& e : entries) {
    entry_map[e.template_id] = e;
  }

  nlohmann::json items = nlohmann::json::array();
  for (const auto& tmpl : all_catalog) {
    // 预览图优先级：
    //   1. FastDFS thumbnail_url（DB 中已同步）
    //   2. catalog 中的 preview_image（外链 URL）
    //   3. 回退到后端本地预览接口
    std::string preview_url;
    if (tmpl_fastdfs_service_) {
      auto fdfs_entry = tmpl_fastdfs_service_->GetEntry(tmpl.id);
      if (fdfs_entry && !fdfs_entry->thumbnail_url.empty()) {
        preview_url = fdfs_entry->thumbnail_url;
      }
    }
    if (preview_url.empty()) {
      preview_url = tmpl.preview_image.empty()
          ? "/api/templates/preview?id=" + tmpl.id
          : tmpl.preview_image;
    }

    // hasLocalFile：本地文件存在 OR FastDFS 已有记录，都视为"可用"
    bool has_fastdfs_pptx = false;
    if (tmpl_fastdfs_service_) {
      auto fdfs = tmpl_fastdfs_service_->GetEntry(tmpl.id);
      if (fdfs && !fdfs->pptx_url.empty()) {
        has_fastdfs_pptx = true;
      }
    }

    nlohmann::json item = {
        {"id",          tmpl.id},
        {"name",        tmpl.name},
        {"provider",    tmpl.provider},
        {"description", tmpl.description},
        {"previewImage", preview_url},
        {"tags",        tmpl.tags},
        {"hasLocalFile", tmpl.has_local_file || has_fastdfs_pptx},
        {"hasFastDfs",   has_fastdfs_pptx}
    };

    // 合并上架状态
    auto it = entry_map.find(tmpl.id);
    if (it != entry_map.end()) {
      const auto& e = it->second;
      item["listing"] = EntryToJson(e);
      item["isListed"] = e.is_active;
    } else {
      item["listing"]  = nullptr;
      item["isListed"] = false;
    }
    items.push_back(item);
  }

  return HttpResponse::Json(200, {{"items", items}});
}

HttpResponse TemplateManagerController::Activate(const HttpRequest& request) {
  std::string error;
  auto admin = AuthenticateAdmin(request, error);
  if (!admin) {
    int code = (error == "Forbidden") ? 403 : 401;
    return HttpResponse::Json(code, ErrorJson("ERR_UNAUTHORIZED", error));
  }

  try {
    auto body = nlohmann::json::parse(request.body);

    if (!body.contains("templateId") || !body["templateId"].is_string()) {
      return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "缺少 templateId"));
    }

    std::string template_id = body["templateId"].get<std::string>();

    // 验证 template_id 在 catalog 中存在
    auto tmpl = template_service_->FindById(template_id);
    if (!tmpl) {
      return HttpResponse::Json(404, ErrorJson("ERR_NOT_FOUND", "模板不存在"));
    }

    TemplateEntry entry;
    entry.template_id   = template_id;
    entry.template_name = tmpl->name;
    entry.is_active     = true;
    entry.created_by    = admin->id;

    // available_from：默认当前时间，可由请求体覆盖
    if (body.contains("availableFrom") && body["availableFrom"].is_string()) {
      entry.available_from = body["availableFrom"].get<std::string>();
    } else {
      // 让数据库使用 DEFAULT CURRENT_TIMESTAMP，传空字符串则用 NOW()
      // 此处给一个 MySQL 接受的格式占位
      entry.available_from = "";  // 在 Upsert 中若为空则用 NOW()
    }

    // available_to：可选，空=永久
    if (body.contains("availableTo") && body["availableTo"].is_string() &&
        !body["availableTo"].get<std::string>().empty()) {
      entry.available_to = body["availableTo"].get<std::string>();
    }

    // 修正：available_from 为空时用 NOW() 字符串
    if (entry.available_from.empty()) {
      // 留给 Upsert 用 CURRENT_TIMESTAMP DEFAULT 处理
      // 使用 NOW() 占位（通过特殊值处理）
      entry.available_from = "NOW()";
    }

    if (!tmpl_mgr_service_->Upsert(entry, error)) {
      Logger::Error("Activate template failed: " + error);
      return HttpResponse::Json(500, ErrorJson("ERR_INTERNAL", "上架失败：" + error));
    }

    if (audit_service_) {
      std::string detail = "{\"templateId\":\"" + template_id + "\"}";
      audit_service_->Write(admin->id, admin->username,
                            "activate_template", "template", template_id,
                            detail, ExtractIp(request));
    }

    return HttpResponse::Json(200, {{"success", true}, {"templateId", template_id}});
  } catch (const std::exception& ex) {
    Logger::Error(std::string("Activate parse error: ") + ex.what());
    return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "Invalid JSON"));
  }
}

HttpResponse TemplateManagerController::Deactivate(const HttpRequest& request) {
  std::string error;
  auto admin = AuthenticateAdmin(request, error);
  if (!admin) {
    int code = (error == "Forbidden") ? 403 : 401;
    return HttpResponse::Json(code, ErrorJson("ERR_UNAUTHORIZED", error));
  }

  try {
    auto body = nlohmann::json::parse(request.body);
    if (!body.contains("templateId") || !body["templateId"].is_string()) {
      return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "缺少 templateId"));
    }
    std::string template_id = body["templateId"].get<std::string>();

    if (!tmpl_mgr_service_->Deactivate(template_id, error)) {
      Logger::Error("Deactivate template failed: " + error);
      return HttpResponse::Json(500, ErrorJson("ERR_INTERNAL", "下架失败：" + error));
    }

    if (audit_service_) {
      std::string detail = "{\"templateId\":\"" + template_id + "\"}";
      audit_service_->Write(admin->id, admin->username,
                            "deactivate_template", "template", template_id,
                            detail, ExtractIp(request));
    }

    return HttpResponse::Json(200, {{"success", true}, {"templateId", template_id}});
  } catch (const std::exception& ex) {
    Logger::Error(std::string("Deactivate parse error: ") + ex.what());
    return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "Invalid JSON"));
  }
}

HttpResponse TemplateManagerController::Remove(const HttpRequest& request) {
  std::string error;
  auto admin = AuthenticateAdmin(request, error);
  if (!admin) {
    int code = (error == "Forbidden") ? 403 : 401;
    return HttpResponse::Json(code, ErrorJson("ERR_UNAUTHORIZED", error));
  }

  std::string template_id;
  if (auto it = request.query_params.find("templateId"); it != request.query_params.end()) {
    template_id = it->second;
  }
  if (template_id.empty()) {
    return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "缺少 templateId 参数"));
  }

  if (!tmpl_mgr_service_->Remove(template_id, error)) {
    Logger::Error("Remove template listing failed: " + error);
    return HttpResponse::Json(500, ErrorJson("ERR_INTERNAL", "删除失败：" + error));
  }

  if (audit_service_) {
    std::string detail = "{\"templateId\":\"" + template_id + "\"}";
    audit_service_->Write(admin->id, admin->username,
                          "remove_template_listing", "template", template_id,
                          detail, ExtractIp(request));
  }

  return HttpResponse::Json(200, {{"success", true}, {"templateId", template_id}});
}

HttpResponse TemplateManagerController::FullDelete(const HttpRequest& request) {
  std::string error;
  auto admin = AuthenticateAdmin(request, error);
  if (!admin) {
    int code = (error == "Forbidden") ? 403 : 401;
    return HttpResponse::Json(code, ErrorJson("ERR_UNAUTHORIZED", error));
  }

  std::string template_id;
  if (auto it = request.query_params.find("templateId"); it != request.query_params.end()) {
    template_id = it->second;
  }
  if (template_id.empty()) {
    return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "缺少 templateId 参数"));
  }

  nlohmann::json steps = nlohmann::json::array();
  bool any_error = false;

  // ── Step 1: FastDFS 文件 + template_fastdfs_map 记录 ─────────────────────
  if (tmpl_fastdfs_service_) {
    std::string fdfs_error;
    if (!tmpl_fastdfs_service_->RemoveEntry(template_id, true, fdfs_error)) {
      Logger::Warn("FullDelete[" + template_id + "] FastDFS/map remove failed: " + fdfs_error);
      steps.push_back({{"step", "fastdfs"}, {"ok", false}, {"msg", fdfs_error}});
      any_error = true;
    } else {
      steps.push_back({{"step", "fastdfs"}, {"ok", true}});
      Logger::Info("FullDelete[" + template_id + "] FastDFS files + map record removed");
    }
  } else {
    steps.push_back({{"step", "fastdfs"}, {"ok", true}, {"msg", "FastDFS 未启用，跳过"}});
  }

  // ── Step 2: template_listings DB 记录 ────────────────────────────────────
  {
    std::string rm_error;
    if (!tmpl_mgr_service_->Remove(template_id, rm_error)) {
      Logger::Warn("FullDelete[" + template_id + "] listing remove failed: " + rm_error);
      steps.push_back({{"step", "listing"}, {"ok", false}, {"msg", rm_error}});
      any_error = true;
    } else {
      steps.push_back({{"step", "listing"}, {"ok", true}});
    }
  }

  // ── Step 3: 本地文件 + catalog 条目 ──────────────────────────────────────
  // 先通过 TemplateService 查本地文件路径
  std::string local_pptx;
  if (template_service_) {
    auto local = template_service_->GetLocalFile(template_id);
    if (local) local_pptx = *local;
  }

  // 查本地缩略图路径
  std::string local_thumb;
  if (template_service_) {
    auto thumb = template_service_->GetPreviewPath(template_id);
    if (thumb) local_thumb = *thumb;
  }

  // 从 catalog 移除（同时写回 templates.json 并热重载）
  if (template_service_) {
    std::string catalog_local_file;
    std::string cat_error;
    if (!template_service_->RemoveFromCatalog(template_id, catalog_local_file, cat_error)) {
      Logger::Warn("FullDelete[" + template_id + "] catalog remove failed: " + cat_error);
      steps.push_back({{"step", "catalog"}, {"ok", false}, {"msg", cat_error}});
      any_error = true;
    } else {
      steps.push_back({{"step", "catalog"}, {"ok", true}});
      // 如果 GetLocalFile 没拿到路径，用 catalog 里的备用
      if (local_pptx.empty() && !catalog_local_file.empty()) {
        local_pptx = catalog_local_file;
      }
    }
  } else {
    steps.push_back({{"step", "catalog"}, {"ok", true}, {"msg", "TemplateService 不可用，跳过"}});
  }

  // ── Step 4: 删除本地 pptx 文件 ───────────────────────────────────────────
  if (!local_pptx.empty()) {
    std::error_code ec;
    if (std::filesystem::remove(local_pptx, ec)) {
      steps.push_back({{"step", "local_pptx"}, {"ok", true}, {"path", local_pptx}});
      Logger::Info("FullDelete[" + template_id + "] removed local pptx: " + local_pptx);
    } else {
      // 文件不存在也视为成功
      steps.push_back({{"step", "local_pptx"}, {"ok", true}, {"msg", "文件不存在或已删除"}});
    }
  } else {
    steps.push_back({{"step", "local_pptx"}, {"ok", true}, {"msg", "无本地 pptx"}});
  }

  // ── Step 5: 删除本地缩略图 ───────────────────────────────────────────────
  if (!local_thumb.empty()) {
    std::error_code ec;
    std::filesystem::remove(local_thumb, ec);
    steps.push_back({{"step", "local_thumb"}, {"ok", true}, {"path", local_thumb}});
    Logger::Info("FullDelete[" + template_id + "] removed local thumb: " + local_thumb);
  } else {
    steps.push_back({{"step", "local_thumb"}, {"ok", true}, {"msg", "无本地缩略图"}});
  }

  // ── 审计日志 ─────────────────────────────────────────────────────────────
  if (audit_service_) {
    const std::string detail = "{\"templateId\":\"" + template_id
                               + "\",\"action\":\"full_delete\"}";
    audit_service_->Write(admin->id, admin->username,
                          "full_delete_template", "template", template_id,
                          detail, ExtractIp(request));
  }

  Logger::Info("FullDelete[" + template_id + "] completed, any_error=" +
               (any_error ? "true" : "false"));

  return HttpResponse::Json(200, {
      {"success",    true},
      {"templateId", template_id},
      {"steps",      steps}
  });
}

HttpResponse TemplateManagerController::SyncThumbnails(const HttpRequest& request) {
  std::string error;
  auto admin = AuthenticateAdmin(request, error);
  if (!admin) {
    int code = (error == "Forbidden") ? 403 : 401;
    return HttpResponse::Json(code, ErrorJson("ERR_UNAUTHORIZED", error));
  }

  if (!tmpl_fastdfs_service_) {
    return HttpResponse::Json(503, ErrorJson("ERR_UNAVAILABLE", "FastDFS 未启用"));
  }

  const auto& all_catalog = template_service_->GetAll();

  // 从 catalog 中获取缩略图本地路径（由 TemplateService::GetPreviewPath 提供）
  // GetPreviewPath 查找 assets/template_thumbnails/{id}.png|jpg|jpeg
  int ok = 0, skipped = 0, failed = 0;
  nlohmann::json results = nlohmann::json::array();

  for (const auto& tmpl : all_catalog) {
    // 已有 FastDFS 缩略图则跳过（除非强制覆盖）
    bool force = false;
    if (auto it = request.query_params.find("force"); it != request.query_params.end()) {
      force = (it->second == "1" || it->second == "true");
    }
    if (!force) {
      auto existing = tmpl_fastdfs_service_->GetEntry(tmpl.id);
      if (existing && !existing->thumbnail_url.empty()) {
        skipped++;
        results.push_back({{"id", tmpl.id}, {"status", "skipped"},
                           {"url", existing->thumbnail_url}});
        continue;
      }
    }

    // 通过 TemplateService 获取本地缩略图路径
    auto preview_path = template_service_->GetPreviewPath(tmpl.id);
    if (!preview_path) {
      skipped++;
      results.push_back({{"id", tmpl.id}, {"status", "no_thumbnail"}});
      continue;
    }

    std::string upload_error;
    TemplateFastDfsService::TemplateEntry entry;
    entry.template_id = tmpl.id;

    const std::string ext = std::filesystem::path(*preview_path).extension().string();
    const std::string ext_noDot = (ext.size() > 1) ? ext.substr(1) : "png";
    std::string file_id;
    // 直接调用 UploadTemplate 的缩略图部分逻辑（通过 UpsertEntry + fastdfs_client）
    // 这里我们用 TemplateFastDfsService::UploadTemplate 传空 pptx 路径
    if (!tmpl_fastdfs_service_->UploadTemplate(tmpl.id, "", *preview_path, upload_error)) {
      failed++;
      results.push_back({{"id", tmpl.id}, {"status", "failed"}, {"error", upload_error}});
      Logger::Warn("SyncThumbnails: " + tmpl.id + " failed: " + upload_error);
    } else {
      ok++;
      auto e = tmpl_fastdfs_service_->GetEntry(tmpl.id);
      std::string url = (e && !e->thumbnail_url.empty()) ? e->thumbnail_url : "";
      results.push_back({{"id", tmpl.id}, {"status", "ok"}, {"url", url}});
      Logger::Info("SyncThumbnails: " + tmpl.id + " -> " + url);
    }
  }

  Logger::Info("SyncThumbnails: ok=" + std::to_string(ok) +
               " skipped=" + std::to_string(skipped) +
               " failed=" + std::to_string(failed));

  return HttpResponse::Json(200, {
    {"success", true},
    {"ok", ok}, {"skipped", skipped}, {"failed", failed},
    {"results", results}
  });
}

HttpResponse TemplateManagerController::SyncStatus(const HttpRequest& request) {
  std::string error;
  auto admin = AuthenticateAdmin(request, error);
  if (!admin) {
    int code = (error == "Forbidden") ? 403 : 401;
    return HttpResponse::Json(code, ErrorJson("ERR_UNAUTHORIZED", error));
  }

  auto it = request.query_params.find("templateId");
  if (it == request.query_params.end() || it->second.empty()) {
    return HttpResponse::Json(400, ErrorJson("ERR_BAD_REQUEST", "缺少 templateId"));
  }
  const std::string template_id = it->second;

  bool has_pptx  = false;
  bool has_thumb = false;
  std::string pptx_url;
  std::string thumb_url;

  if (tmpl_fastdfs_service_) {
    auto entry = tmpl_fastdfs_service_->GetEntry(template_id);
    if (entry) {
      has_pptx  = !entry->pptx_url.empty();
      has_thumb = !entry->thumbnail_url.empty();
      pptx_url  = entry->pptx_url;
      thumb_url = entry->thumbnail_url;
    }
  }

  return HttpResponse::Json(200, {
    {"templateId", template_id},
    {"synced",     has_pptx},
    {"hasPptx",    has_pptx},
    {"hasThumb",   has_thumb},
    {"pptxUrl",    pptx_url},
    {"thumbUrl",   thumb_url},
  });
}
