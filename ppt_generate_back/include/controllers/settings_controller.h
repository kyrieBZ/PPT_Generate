#pragma once

#include <memory>
#include <string>

#include "database/mysql_connection_pool.h"
#include "http/http_types.h"
#include "services/auth_service.h"
#include "services/audit_service.h"

/**
 * SettingsController — 系统配置中心
 *
 * GET  /api/admin/settings       — 返回所有可配置项
 * PUT  /api/admin/settings       — 批量更新配置项 (body: { key: value, ... })
 */
class SettingsController {
 public:
  SettingsController(std::shared_ptr<AuthService>         auth_service,
                     std::shared_ptr<MySQLConnectionPool> pool,
                     std::shared_ptr<AuditService>        audit_service);

  /** GET /api/admin/settings — 返回所有配置项（含元数据） */
  HttpResponse GetSettings(const HttpRequest& request);

  /** PUT /api/admin/settings — 批量更新配置项 */
  HttpResponse UpdateSettings(const HttpRequest& request);

 private:
  std::shared_ptr<User> AuthenticateAdmin(const HttpRequest& request,
                                          HttpResponse&       error_response) const;

  /** 确保 system_settings 表存在并填充缺失的默认配置项 */
  void EnsureDefaultSettings(MYSQL* conn) const;

  std::shared_ptr<AuthService>         auth_service_;
  std::shared_ptr<MySQLConnectionPool> pool_;
  std::shared_ptr<AuditService>        audit_service_;
};
