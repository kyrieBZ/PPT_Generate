#pragma once

#include <memory>
#include <string>

#include "http/http_types.h"
#include "services/auth_service.h"
#include "services/audit_service.h"

class AuditController {
 public:
  AuditController(std::shared_ptr<AuthService> auth_service,
                  std::shared_ptr<AuditService> audit_service);

  /** GET /api/admin/audit_logs — 分页查询，支持 ?action= ?start= ?end= ?q= ?page= ?page_size= */
  HttpResponse AdminList(const HttpRequest& request);

  /** GET /api/admin/audit_logs/export — 导出 CSV（不分页，最多 5000 条）*/
  HttpResponse AdminExport(const HttpRequest& request);

 private:
  std::shared_ptr<User> AuthenticateAdmin(const HttpRequest& request, std::string& error) const;

  std::shared_ptr<AuthService>  auth_service_;
  std::shared_ptr<AuditService> audit_service_;
};
