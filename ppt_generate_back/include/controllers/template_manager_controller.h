#pragma once

#include <memory>
#include <string>

#include "http/http_types.h"
#include "services/auth_service.h"
#include "services/audit_service.h"
#include "services/template_manager_service.h"
#include "services/template_service.h"

/**
 * 管理员端模板管理控制器
 *
 * GET    /api/admin/templates          — 获取全部模板（含上架状态）
 * POST   /api/admin/templates/activate — 上架/更新模板
 * POST   /api/admin/templates/deactivate — 下架模板
 * DELETE /api/admin/templates          — 删除模板记录
 */
class TemplateManagerController {
 public:
  TemplateManagerController(std::shared_ptr<AuthService>            auth_service,
                             std::shared_ptr<AuditService>           audit_service,
                             std::shared_ptr<TemplateManagerService> tmpl_mgr_service,
                             std::shared_ptr<TemplateService>        template_service);

  /** GET /api/admin/templates — 返回 catalog 中所有模板，并附加上架状态信息 */
  HttpResponse AdminList(const HttpRequest& request);

  /** POST /api/admin/templates/activate — 上架或更新模板 */
  HttpResponse Activate(const HttpRequest& request);

  /** POST /api/admin/templates/deactivate — 下架模板 */
  HttpResponse Deactivate(const HttpRequest& request);

  /** DELETE /api/admin/templates — 删除模板记录 */
  HttpResponse Remove(const HttpRequest& request);

 private:
  std::shared_ptr<User> AuthenticateAdmin(const HttpRequest& request, std::string& error) const;
  static std::string ExtractIp(const HttpRequest& request);

  std::shared_ptr<AuthService>            auth_service_;
  std::shared_ptr<AuditService>           audit_service_;
  std::shared_ptr<TemplateManagerService> tmpl_mgr_service_;
  std::shared_ptr<TemplateService>        template_service_;
};
