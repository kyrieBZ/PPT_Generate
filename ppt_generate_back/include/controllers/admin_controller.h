#pragma once

#include <memory>
#include <string>

#include "http/http_types.h"
#include "services/auth_service.h"
#include "services/audit_service.h"

class AdminController {
 public:
  AdminController(std::shared_ptr<AuthService>  auth_service,
                  std::shared_ptr<AuditService> audit_service);

  HttpResponse ListUsers(const HttpRequest& request);
  HttpResponse UpdateUserStatus(const HttpRequest& request);
  /** POST /api/admin/users/batch_status — 批量禁用/启用用户
   *  body: { "ids": [1,2,3], "disabled": true/false }
   */
  HttpResponse BatchUpdateUserStatus(const HttpRequest& request);
  /** GET /api/admin/export/users — 导出用户列表 CSV（支持 ?q= 筛选）*/
  HttpResponse ExportUsers(const HttpRequest& request);

 private:
  std::shared_ptr<User> AuthenticateAdmin(const HttpRequest& request, std::string& error) const;

  /** 从请求头中提取客户端 IP（优先 X-Forwarded-For，降级为 unknown）。*/
  static std::string ExtractIp(const HttpRequest& request);

  std::shared_ptr<AuthService>  auth_service_;
  std::shared_ptr<AuditService> audit_service_;
};
