#include "controllers/template_manager_controller.h"

#include <unordered_map>

#include <nlohmann/json.hpp>

#include "http/http_types.h"
#include "logger.h"
#include "utils/string_utils.h"

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
    std::shared_ptr<TemplateService>        template_service)
    : auth_service_(std::move(auth_service)),
      audit_service_(std::move(audit_service)),
      tmpl_mgr_service_(std::move(tmpl_mgr_service)),
      template_service_(std::move(template_service)) {}

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
    // 预览图：优先使用 catalog 中的 preview_image，否则回退到本地预览接口
    std::string preview_url = tmpl.preview_image.empty()
        ? "/api/templates/preview?id=" + tmpl.id
        : tmpl.preview_image;

    nlohmann::json item = {
        {"id",          tmpl.id},
        {"name",        tmpl.name},
        {"provider",    tmpl.provider},
        {"description", tmpl.description},
        {"previewImage", preview_url},
        {"tags",        tmpl.tags},
        {"hasLocalFile", tmpl.has_local_file}
    };

    // 合并上架状态
    auto it = entry_map.find(tmpl.id);
    if (it != entry_map.end()) {
      const auto& e = it->second;
      item["listing"] = EntryToJson(e);
      // 判断是否当前有效（is_active=true 且在时间范围内）
      // 简单标记，前端可自行判断
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
