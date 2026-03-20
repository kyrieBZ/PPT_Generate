#pragma once

#include <memory>

#include "http/http_types.h"
#include "services/auth_service.h"
#include "services/audit_service.h"
#include "database/mysql_connection_pool.h"

/**
 * AnnouncementController — 公告与通知管理
 *
 *  公开（登录用户）:
 *    GET  /api/announcements           — 当前有效公告列表
 *
 *  管理员专用:
 *    GET    /api/admin/announcements   — 全量公告列表（含已过期）
 *    POST   /api/admin/announcements   — 创建公告
 *    PUT    /api/admin/announcements   — 更新公告 (?id=xxx)
 *    DELETE /api/admin/announcements   — 删除公告 (?id=xxx)
 */
class AnnouncementController {
 public:
  AnnouncementController(std::shared_ptr<AuthService>         auth_service,
                         std::shared_ptr<MySQLConnectionPool> pool,
                         std::shared_ptr<AuditService>        audit_service = nullptr);

  /** GET /api/announcements — 公开，返回当前有效公告（starts_at<=now, expires_at IS NULL OR >now） */
  HttpResponse ListActive(const HttpRequest& request);

  /** GET /api/admin/announcements — 管理员查看全量公告列表 */
  HttpResponse AdminList(const HttpRequest& request);

  /** POST /api/admin/announcements — 创建公告 */
  HttpResponse AdminCreate(const HttpRequest& request);

  /** PUT /api/admin/announcements — 更新公告 (?id=xxx) */
  HttpResponse AdminUpdate(const HttpRequest& request);

  /** DELETE /api/admin/announcements — 删除公告 (?id=xxx) */
  HttpResponse AdminDelete(const HttpRequest& request);

 private:
  /** 验证管理员身份，返回 User 指针；失败返回 nullptr 并填充 error_response */
  std::shared_ptr<User> AuthenticateAdmin(const HttpRequest& request,
                                          HttpResponse& error_response) const;

  std::shared_ptr<AuthService>         auth_service_;
  std::shared_ptr<MySQLConnectionPool> pool_;
  std::shared_ptr<AuditService>        audit_service_;
};
