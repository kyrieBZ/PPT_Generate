#pragma once

#include <memory>

#include "http/http_types.h"
#include "services/auth_service.h"
#include "services/material_service.h"
#include "utils/thread_pool.h"

class MaterialController {
 public:
  MaterialController(std::shared_ptr<AuthService> auth_service,
                     std::shared_ptr<MaterialService> material_service,
                     std::shared_ptr<ThreadPool> thread_pool,
                     std::string qwen_api_key = {},
                     std::uint32_t qwen_timeout_sec = 60);

  /** POST /api/material/upload  — multipart/form-data */
  HttpResponse Upload(const HttpRequest& request);

  /** GET /api/material/status?id= */
  HttpResponse GetStatus(const HttpRequest& request);

  /** GET /api/material/result?id= */
  HttpResponse GetResult(const HttpRequest& request);

  /** PUT /api/material/result?id= */
  HttpResponse SaveResult(const HttpRequest& request);

  /** GET /api/material/list */
  HttpResponse List(const HttpRequest& request);

  /** DELETE /api/material?id= */
  HttpResponse Delete(const HttpRequest& request);

  /** POST /api/material/batch_delete — body: {"ids":["id1","id2",...]}，批量删除 */
  HttpResponse BatchDelete(const HttpRequest& request);

  /** POST /api/material/batch_upload — multipart/form-data，多文件 */
  HttpResponse BatchUpload(const HttpRequest& request);

  /** GET /api/material/batch_status?ids=id1,id2,... */
  HttpResponse BatchStatus(const HttpRequest& request);

  /** GET /api/admin/materials — 管理员查看全量素材（分页+筛选） */
  HttpResponse AdminList(const HttpRequest& request);

  /** GET /api/admin/materials/stats — 管理员素材存储统计 */
  HttpResponse AdminStats(const HttpRequest& request);

  /** GET /api/admin/materials/content — 管理员预览素材提取内容 (?id=xxx) */
  HttpResponse AdminGetContent(const HttpRequest& request);

  /** GET /api/admin/materials/file — 管理员下载/内联预览原始文件 (?id=xxx) */
  HttpResponse AdminGetFile(const HttpRequest& request);

  /** POST /api/admin/materials/review — 管理员触发 AI 违规审核 (?id=xxx) */
  HttpResponse AdminReview(const HttpRequest& request);

  /** DELETE /api/admin/materials — 管理员强制删除素材 (?id=xxx)，body: {"reason":"..."} */
  HttpResponse AdminDelete(const HttpRequest& request);

  /** POST /api/admin/materials/batch_delete — 管理员批量删除素材，body: {"ids":[...],"reason":"..."} */
  HttpResponse AdminBatchDelete(const HttpRequest& request);

  /** GET /api/material/notices — 用户获取未读删除通知 */
  HttpResponse GetDeletionNotices(const HttpRequest& request);

  /** POST /api/material/notices/read — 用户标记通知已读，body: {"ids":[...]} 空数组=全部已读 */
  HttpResponse MarkNoticesRead(const HttpRequest& request);

 private:
  std::shared_ptr<User> Authenticate(const HttpRequest& request, std::string& error) const;

  std::shared_ptr<AuthService> auth_service_;
  std::shared_ptr<MaterialService> material_service_;
  std::shared_ptr<ThreadPool> thread_pool_;
  std::string qwen_api_key_;
  std::uint32_t qwen_timeout_sec_ = 60;
};
